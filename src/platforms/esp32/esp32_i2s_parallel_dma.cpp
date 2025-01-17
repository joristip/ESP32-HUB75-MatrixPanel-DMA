/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)

Contributors:
 [ciniml](https://github.com/ciniml)
 [mongonta0716](https://github.com/mongonta0716)
 [tobozo](https://github.com/tobozo)

Modified heavily for the ESP32 HUB75 DMA library by:
 [mrfaptastic](https://github.com/mrfaptastic)
  
/----------------------------------------------------------------------------*/

static const char* TAG = "esp32_i2s_parallel_dma";

#include <sdkconfig.h>
#if defined (CONFIG_IDF_TARGET_ESP32)

#include "esp32_i2s_parallel_dma.hpp"

#include <driver/gpio.h>
#include <driver/periph_ctrl.h>
#include <soc/gpio_sig_map.h>

#include <esp_err.h>
#include <esp_log.h>

/*

callback shiftCompleteCallback;
void setShiftCompleteCallback(callback f) {
    shiftCompleteCallback = f;
}

volatile int  previousBufferOutputLoopCount = 0;
volatile bool previousBufferFree      = true;

static void IRAM_ATTR irq_hndlr(void* arg) { // if we use I2S1 (default)

        SET_PERI_REG_BITS(I2S_INT_CLR_REG(ESP32_I2S_DEVICE), I2S_OUT_EOF_INT_CLR_V, 1, I2S_OUT_EOF_INT_CLR_S);

	previousBufferFree 		= true;


        
} // end irq_hndlr
*/


  static i2s_dev_t* getDev(int port)
  {
      #if defined (CONFIG_IDF_TARGET_ESP32S2)
          return &I2S0;
      #else
          return (port == 0) ? &I2S0 : &I2S1;
      #endif
  }

  void Bus_Parallel16::config(const config_t& cfg)
  {
      ESP_LOGI(TAG, "Performing config for ESP32 or ESP32-S2");
      _cfg = cfg;
      auto port = cfg.port;
      _dev = getDev(port);
  }


//#if defined (CONFIG_IDF_TARGET_ESP32S2)  

  static void _gpio_pin_init(int pin)
  {
    if (pin >= 0)
    {
      gpio_pad_select_gpio(pin);
      //gpio_hi(pin);
      gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
      gpio_set_drive_capability((gpio_num_t)pin, (gpio_drive_cap_t)3);      // esp32s3 as well?
    }
  }

 
 bool Bus_Parallel16::init(void) // The big one that gets everything setup.
 {
    ESP_LOGI(TAG, "Performing DMA bus init() for ESP32 or ESP32-S2");

    if(_cfg.port < I2S_NUM_0 || _cfg.port >= I2S_NUM_MAX) {
      //return ESP_ERR_INVALID_ARG;
      return false;
    }

    if(_cfg.parallel_width < 8 || _cfg.parallel_width >= 24) {
      return false;
    }

    //auto freq = (_cfg.freq_write, 50000000u); // ?
    auto freq = (_cfg.bus_freq);
    
    uint32_t _clkdiv_write = 0;
    size_t _div_num        = 10;

    // Calculate clock divider for ESP32-S2
    #if defined (CONFIG_IDF_TARGET_ESP32S2)      

    static constexpr uint32_t pll_160M_clock_d2 = 160 * 1000 * 1000 >> 1;

    // I2S_CLKM_DIV_NUM 2=40MHz  /  3=27MHz  /  4=20MHz  /  5=16MHz  /  8=10MHz  /  10=8MHz
    _div_num = std::min(255u, 1 + ((pll_160M_clock_d2) / (1 + _cfg.freq_write)));

    _clkdiv_write = I2S_CLK_160M_PLL << I2S_CLK_SEL_S
                  |             I2S_CLK_EN
                  |        1 << I2S_CLKM_DIV_A_S
                  |        0 << I2S_CLKM_DIV_B_S
                  | _div_num << I2S_CLKM_DIV_NUM_S
                  ;

    #else 


    // clock = 80MHz(PLL_D2_CLK)
    static constexpr uint32_t pll_d2_clock = 80 * 1000 * 1000;

    // I2S_CLKM_DIV_NUM 4=20MHz  /  5=16MHz  /  8=10MHz  /  10=8MHz
    _div_num = std::min(255u, std::max(3u, 1 + (pll_d2_clock / (1 + freq))));

    _clkdiv_write =             I2S_CLK_EN
                  |        1 << I2S_CLKM_DIV_A_S
                  |        0 << I2S_CLKM_DIV_B_S
                  | _div_num << I2S_CLKM_DIV_NUM_S
                  ;
    #endif
 
    if(_div_num < 2 || _div_num > 16) {
      return false;
    }

    //ESP_LOGI(TAG, "i2s pll clk_div_main is: %d", _div_num);    

    auto dev = _dev;
    volatile int iomux_signal_base;
    volatile int iomux_clock;
    int irq_source;

    // Initialize I2S0 peripheral
    if (_cfg.port == 0) 
    {
      
        periph_module_reset(PERIPH_I2S0_MODULE);
        periph_module_enable(PERIPH_I2S0_MODULE);

        iomux_clock = I2S0O_WS_OUT_IDX;
        irq_source = ETS_I2S0_INTR_SOURCE;

        switch(_cfg.parallel_width) {
          case 8:
          case 16:
            iomux_signal_base = I2S0O_DATA_OUT8_IDX;
            break;
          case 24:
            iomux_signal_base = I2S0O_DATA_OUT0_IDX;
            break;
          default:
            return ESP_ERR_INVALID_ARG;
        }
    } 

    #if !defined (CONFIG_IDF_TARGET_ESP32S2)  
    // Can't compile if I2S1 if it doesn't exist with that hardware's IDF.... 
    else {
        periph_module_reset(PERIPH_I2S1_MODULE);
        periph_module_enable(PERIPH_I2S1_MODULE);
        iomux_clock = I2S1O_WS_OUT_IDX;
        irq_source = ETS_I2S1_INTR_SOURCE;

        switch(_cfg.parallel_width) {
          case 16:
            iomux_signal_base = I2S1O_DATA_OUT8_IDX;
            break;
          case 8:
          case 24:
            iomux_signal_base = I2S1O_DATA_OUT0_IDX;
            break;
          default:
            return ESP_ERR_INVALID_ARG;
        }
    }
    #endif 

    // Setup GPIOs
    int bus_width = _cfg.parallel_width;

    // Clock output GPIO setup
    _gpio_pin_init(_cfg.pin_rd); // not used
    _gpio_pin_init(_cfg.pin_wr); // clock
    _gpio_pin_init(_cfg.pin_rs); // not used

    // Data output GPIO setup
    int8_t* pins = _cfg.pin_data;  

    for(int i = 0; i < bus_width; i++) 
     _gpio_pin_init(pins[i]);

    // Route clock signal to clock pin
    gpio_matrix_out(_cfg.pin_wr, iomux_clock, _cfg.invert_pclk, 0); // inverst clock if required
  
    for (size_t i = 0; i < bus_width; i++) {

      if (pins[i] >= 0) {
        gpio_matrix_out(pins[i], iomux_signal_base + i, false, false);
      }
    }
        
 
    // Setup i2s clock
    dev->sample_rate_conf.val = 0;
    
    // Third stage config, width of data to be written to IO (I think this should always be the actual data width?)
    dev->sample_rate_conf.rx_bits_mod = bus_width;
    dev->sample_rate_conf.tx_bits_mod = bus_width;
    
    dev->sample_rate_conf.rx_bck_div_num = 2;
    dev->sample_rate_conf.tx_bck_div_num = 2;
    
    // Clock configuration
   // dev->clkm_conf.val=0;             // Clear the clkm_conf struct  
    /*
  #if defined (CONFIG_IDF_TARGET_ESP32S2)  
    dev->clkm_conf.clk_sel = 2; // esp32-s2 only  
    dev->clkm_conf.clk_en  = 1;
  #endif

  #if !defined (CONFIG_IDF_TARGET_ESP32S2)  
    dev->clkm_conf.clka_en=0;         // Use the 80mhz system clock (PLL_D2_CLK) when '0'
  #endif
    
    dev->clkm_conf.clkm_div_b=0;      // Clock numerator
    dev->clkm_conf.clkm_div_a=1;      // Clock denominator  
    */

    // Note: clkm_div_num must only be set here AFTER clkm_div_b, clkm_div_a, etc. Or weird things happen!
    // On original ESP32, max I2S DMA parallel speed is 20Mhz.  
    //dev->clkm_conf.clkm_div_num = 32;
    dev->clkm_conf.val = _clkdiv_write;

    // I2S conf2 reg
    dev->conf2.val = 0;
    dev->conf2.lcd_en = 1;
    dev->conf2.lcd_tx_wrx2_en=0; 
    dev->conf2.lcd_tx_sdx2_en=0;    

    // I2S conf reg
    dev->conf.val = 0;   
    
  #if defined (CONFIG_IDF_TARGET_ESP32S2)  
    dev->conf.tx_dma_equal=1;  // esp32-s2 only
    dev->conf.pre_req_en=1;    // esp32-s2 only - enable I2S to prepare data earlier? wtf?
  #endif

    // Now start setting up DMA FIFO
    dev->fifo_conf.val = 0;  
    dev->fifo_conf.rx_data_num = 32; // Thresholds. 
    dev->fifo_conf.tx_data_num = 32;  
    dev->fifo_conf.dscr_en     = 1;  

  #if !defined (CONFIG_IDF_TARGET_ESP32S2)  

    // Enable "One datum will be written twice in LCD mode" - for some reason, 
    // if we don't do this in 8-bit mode, data is updated on half-clocks not clocks
    if(_cfg.parallel_width == 8)
      dev->conf2.lcd_tx_wrx2_en=1;  

    // Not really described for non-pcm modes, although datasheet states it should be set correctly even for LCD mode
    // First stage config. Configures how data is loaded into fifo
    if(_cfg.parallel_width == 24) {
      // Mode 0, single 32-bit channel, linear 32 bit load to fifo
      dev->fifo_conf.tx_fifo_mod = 3;
    } else {
      // Mode 1, single 16-bit channel, load 16 bit sample(*) into fifo and pad to 32 bit with zeros
      // *Actually a 32 bit read where two samples are read at once. Length of fifo must thus still be word-aligned
      dev->fifo_conf.tx_fifo_mod = 1;
    }
  
    // Dictated by ESP32 datasheet
    dev->fifo_conf.rx_fifo_mod_force_en = 1;
    dev->fifo_conf.tx_fifo_mod_force_en = 1;
    
    // Second stage config
    dev->conf_chan.val = 0;
    
    // 16-bit single channel data
    dev->conf_chan.tx_chan_mod = 1;
    dev->conf_chan.rx_chan_mod = 1;
      
  #endif  

    // Reset FIFO
    dev->conf.rx_fifo_reset = 1;
    
  #if defined (CONFIG_IDF_TARGET_ESP32S2)
    while(dev->conf.rx_fifo_reset_st); // esp32-s2 only
  #endif

    dev->conf.rx_fifo_reset = 0;
    dev->conf.tx_fifo_reset = 1;

  #if defined (CONFIG_IDF_TARGET_ESP32S2)
    while(dev->conf.tx_fifo_reset_st); // esp32-s2 only
  #endif
    dev->conf.tx_fifo_reset = 0;


    // Reset DMA
    dev->lc_conf.in_rst = 1;
    dev->lc_conf.in_rst = 0;
    dev->lc_conf.out_rst = 1;
    dev->lc_conf.out_rst = 0;
    
    dev->lc_conf.ahbm_rst = 1;
    dev->lc_conf.ahbm_rst = 0;

    dev->in_link.val = 0;
    dev->out_link.val = 0;    


    // Device reset
    dev->conf.rx_reset=1;
    dev->conf.tx_reset=1;
    dev->conf.rx_reset=0;
    dev->conf.tx_reset=0;  

    dev->conf1.val = 0;
    dev->conf1.tx_stop_en = 0; 
/*    
    // Allocate I2S status structure for buffer swapping stuff
    i2s_state = (i2s_parallel_state_t*) malloc(sizeof(i2s_parallel_state_t));
    assert(i2s_state != NULL);
    i2s_parallel_state_t *state = i2s_state;
      
    state->desccount_a    = conf->desccount_a;
    state->desccount_b    = conf->desccount_b;
    state->dmadesc_a      = conf->lldesc_a;
    state->dmadesc_b      = conf->lldesc_b;  
    state->i2s_interrupt_port_arg  = port; // need to keep this somewhere in static memory for the ISR
*/

    dev->timing.val = 0;
/*
    // We using the double buffering switch logic?
    if (conf->int_ena_out_eof)
    {
        // Get ISR setup 
        esp_err_t err =  esp_intr_alloc(irq_source, 
                                      (int)(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1),
                                      irq_hndlr, 
                                      &state->i2s_interrupt_port_arg, NULL);
        
        if(err) {
            return err;
        }

        
        // Setup interrupt handler which is focussed only on the (page 322 of Tech. Ref. Manual)
        // "I2S_OUT_EOF_INT: Triggered when rxlink has finished sending a packet"
        // ... whatever the hell that is supposed to mean... One massive linked list? So all pixels in the chain?
        dev->int_ena.out_eof = 1;
    }
*/

      
  #if defined (CONFIG_IDF_TARGET_ESP32S2)
      ESP_LOGD(TAG, "init() GPIO and clock configuration set for ESP32-S2");    
  #else
      ESP_LOGD(TAG, "init() GPIO and clock configuration set for ESP32");    
  #endif


      return true;
  }


  void Bus_Parallel16::release(void)
  {
    if (_dmadesc_a)
    {
      heap_caps_free(_dmadesc_a);
      _dmadesc_a = nullptr;
      _dmadesc_count = 0;
    }

    if (_dmadesc_b)
    {
      heap_caps_free(_dmadesc_b);
      _dmadesc_b = nullptr;
      _dmadesc_count = 0;      
    }
  }

  void Bus_Parallel16::enable_double_dma_desc(void)
  {
    _double_dma_buffer = true;
  }

  // Need this to work for double buffers etc.
  bool Bus_Parallel16::allocate_dma_desc_memory(size_t len)
  {
    if (_dmadesc_a) heap_caps_free(_dmadesc_a); // free all dma descrptios previously
    
    _dmadesc_count = len; 

    ESP_LOGI(TAG, "Allocating memory for %d DMA descriptors.", len);    

    _dmadesc_a= (HUB75_DMA_DESCRIPTOR_T*)heap_caps_malloc(sizeof(HUB75_DMA_DESCRIPTOR_T) * len, MALLOC_CAP_DMA);

    if (_dmadesc_a == nullptr)
    {
      ESP_LOGE(TAG, "ERROR: Couldn't malloc _dmadesc_a. Not enough memory.");
      return false;
    }


    if (_double_dma_buffer)
    {
      if (_dmadesc_b) heap_caps_free(_dmadesc_b); // free all dma descrptios previously

      ESP_LOGD(TAG, "Allocating the second buffer (double buffer enabled).");              

      _dmadesc_b= (HUB75_DMA_DESCRIPTOR_T*)heap_caps_malloc(sizeof(HUB75_DMA_DESCRIPTOR_T) * len, MALLOC_CAP_DMA);

      if (_dmadesc_b == nullptr)
      {
        ESP_LOGE(TAG, "ERROR: Couldn't malloc _dmadesc_b. Not enough memory.");
        _double_dma_buffer = false;
        return false;
      }
    }
    
    _dmadesc_a_idx  = 0;
    _dmadesc_b_idx  = 0;

    ESP_LOGD(TAG, "Allocating %d bytes of memory for DMA descriptors.", sizeof(HUB75_DMA_DESCRIPTOR_T) * len);       
   

    return true;

  }

  void Bus_Parallel16::create_dma_desc_link(void *data, size_t size, bool dmadesc_b)
  {
    static constexpr size_t MAX_DMA_LEN = (4096-4);

    if (dmadesc_b)
      ESP_LOGI(TAG, "   * Double buffer descriptor.");            

    if (size > MAX_DMA_LEN)
    {
      size = MAX_DMA_LEN;
      ESP_LOGW(TAG, "Creating DMA descriptor which links to payload with size greater than MAX_DMA_LEN!");            
    }

    if ( !dmadesc_b )
    {
      if ( (_dmadesc_a_idx+1) > _dmadesc_count) {
        ESP_LOGE(TAG, "Attempted to create more DMA descriptors than allocated memory for. Expecting a maximum of %d DMA descriptors", _dmadesc_count);          
        return;
      }
    }

    volatile lldesc_t *dmadesc;
    volatile lldesc_t *next;
    bool eof = false;

/*
    dmadesc_a[desccount-1].eof = 1;
    dmadesc_a[desccount-1].qe.stqe_next=(lldesc_t*)&dmadesc_a[0];
*/


    //  ESP_LOGI(TAG, "Creating descriptor %d\n", _dmadesc_a_idx);    
    if ( (dmadesc_b == true) ) // for primary buffer
    {
        dmadesc      = &_dmadesc_b[_dmadesc_b_idx];

        next = (_dmadesc_b_idx < (_dmadesc_count-1) ) ? &_dmadesc_b[_dmadesc_b_idx+1]:_dmadesc_b;       
        eof  = (_dmadesc_b_idx == (_dmadesc_count-1));
    }
    else
    {
        dmadesc      = &_dmadesc_a[_dmadesc_a_idx];

        // https://stackoverflow.com/questions/47170740/c-negative-array-index
        next = (_dmadesc_a_idx < (_dmadesc_count-1) ) ? _dmadesc_a + _dmadesc_a_idx+1:_dmadesc_a;       
        eof  = (_dmadesc_a_idx == (_dmadesc_count-1));
    }

    if ( _dmadesc_a_idx == (_dmadesc_count-1) ) {
      ESP_LOGW(TAG, "Creating final DMA descriptor and linking back to 0.");             
    } 

    dmadesc->size     = size;
    dmadesc->length   = size;
    dmadesc->buf      = (uint8_t*) data; 
    dmadesc->eof      = 0;         
    dmadesc->sosf     = 0;         
    dmadesc->owner    = 1;         
    dmadesc->qe.stqe_next = (lldesc_t*) next;         
    dmadesc->offset   = 0;       

    if ( (dmadesc_b == true) ) { // for primary buffer
      _dmadesc_b_idx++; 
    } else {
      _dmadesc_a_idx++; 
    }
  
  } // end create_dma_desc_link

  void Bus_Parallel16::dma_transfer_start()
  {
    auto dev = _dev;
   
    // Configure DMA burst mode
    dev->lc_conf.val = I2S_OUT_DATA_BURST_EN | I2S_OUTDSCR_BURST_EN;

    // Set address of DMA descriptor
    dev->out_link.addr = (uint32_t) _dmadesc_a;
  
  // Start DMA operation
    dev->out_link.stop  = 0; 
    dev->out_link.start = 1;
    
    dev->conf.tx_start  = 1;

   
  } // end 
  

  void Bus_Parallel16::dma_transfer_stop()
  {
     auto dev = _dev;
     
    // Stop all ongoing DMA operations
    dev->out_link.stop = 1;
    dev->out_link.start = 0;
    dev->conf.tx_start = 0;
        
  } // end   


  void Bus_Parallel16::set_dma_output_buffer(bool dmadesc_b)
  {
    if ( _double_dma_buffer == false) return;

    if ( dmadesc_b == true) // change across to everything 'b''
    {
        _dmadesc_a[_dmadesc_count-1].qe.stqe_next = &_dmadesc_b[0]; 
        _dmadesc_b[_dmadesc_count-1].qe.stqe_next = &_dmadesc_b[0]; 
    }
    else
    {
        _dmadesc_a[_dmadesc_count-1].qe.stqe_next = &_dmadesc_a[0]; 
        _dmadesc_b[_dmadesc_count-1].qe.stqe_next = &_dmadesc_a[0]; 
    }

    //_dmadesc_a_active ^= _dmadesc_a_active;
    
  } // end flip



#endif
/**
 * @file amux.h
 * @brief Analog multiplexer (AMUX) interface and ADC control
 * 
 * Declares functions for analog multiplexer control and ADC measurements.
 * 
 * @author Bus Pirate Project  
 * @date 2024-2026
 */

/**
 * @brief Initialize analog multiplexer and ADC
 */
void amux_init(void);

/**
 * @brief Select AMUX input channel
 * @param channel Channel number (platform-specific)
 * @return true if successful, false if scope running
 * @note Only affects 4067 analog mux, not current sense
 */
bool amux_select_input(uint16_t channel);

/**
 * @brief Select AMUX input using BIO pin number
 * @param bio BIO pin number (0-7)
 * @return true if successful, false if scope running
 */
bool amux_select_bio(uint8_t bio);

/**
 * @brief Read voltage from AMUX channel
 * @param channel Channel number
 * @return 12-bit ADC value (0-4095)
 */
uint32_t amux_read(uint8_t channel);

/**
 * @brief Read from currently selected AMUX channel
 * @return 12-bit ADC value (0-4095)
 */
uint32_t amux_read_present_channel(void);

/**
 * @brief Read voltage from BIO pin
 * @param bio BIO pin number (0-7)
 * @return 12-bit ADC value (0-4095)
 */
uint32_t amux_read_bio(uint8_t bio);

/**
 * @brief Read current sense (separate ADC channel)
 * @return 12-bit ADC value (0-4095)
 * @note Voltage not divided by 2, uses full ADC range
 */
uint32_t amux_read_current(void);

/**
 * @brief Read all AMUX channels and current sense
 * 
 * Sweeps all channels and stores results in global arrays:
 * - hw_adc_raw[]: Raw ADC values
 * - hw_adc_voltage[]: Converted voltages
 */
void amux_sweep(void);

/**
 * @brief Control ADC busy/lock state
 * @param enable true to acquire lock, false to release
 */
/* Returns false on contention timeout; caller must not touch ADC on failure.
 * Reentrant for the owning core. Legacy scalar reads return zero on failure. */
bool adc_busy_wait(bool enable);
bool adc_try_acquire(void);

typedef enum amux_state {
    AMUX_STATE_STOPPED = 0,
    AMUX_STATE_IDLE,
    AMUX_STATE_SAMPLING,
    AMUX_STATE_PAUSING,
    AMUX_STATE_MANUAL,
    AMUX_STATE_FAULT
} amux_state_t;

typedef struct amux_frame {
    uint64_t timestamp_us;
    uint32_t generation;
    uint16_t raw[HW_ADC_MUX_COUNT + 1];
    bool valid;
    uint8_t fault_code;
} amux_frame_t;

/**
 * @brief Initialize autonomous DMA-driven AMUX sweeping
 * @return true if DMA channels acquired and initialized, false otherwise
 */
bool amux_dma_init(void);

/**
 * @brief Release AMUX DMA resources
 */
void amux_dma_deinit(void);

/**
 * @brief Check if AMUX DMA pipeline is initialized and enabled
 */
bool amux_dma_is_enabled(void);

/**
 * @brief Check if autonomous DMA sweep is active / busy
 * @return true if currently transferring
 */
bool amux_dma_is_busy(void);

#define AMUX_MAX_FRAME_AGE_US 250000ULL

#define AMUX_FAULT_NONE       0
#define AMUX_FAULT_OVERRUN    1
#define AMUX_FAULT_STALL      2
#define AMUX_FAULT_TIMEOUT    3
#define AMUX_FAULT_RESOURCE   4

/**
 * @brief Check if published AMUX telemetry is valid and within maximum age
 * @param max_age_us Maximum allowed age in microseconds
 * @return true if telemetry is valid and fresh, false if stale, uninitialized or faulted
 */
bool amux_is_telemetry_fresh(uint64_t max_age_us);

/**
 * @brief Inject a pipeline fault for testing and failure-recovery validation
 * @param fault_code AMUX_FAULT_* identifier
 */
void amux_inject_fault(uint8_t fault_code);

/**
 * @brief Clear an injected pipeline fault
 */
void amux_clear_fault(void);

/**
 * @brief Get current autonomous AMUX state
 */
amux_state_t amux_get_state(void);

/**
 * @brief Get current published generation count
 */
uint32_t amux_get_generation(void);

/**
 * @brief Get pipeline fault count
 */
uint32_t amux_get_fault_count(void);

/**
 * @brief Get coherent timestamped frame from DMA buffer (seqlock-style)
 * @param out_frame Pointer to caller frame buffer
 * @return true if complete fresh frame extracted, false if lapped or invalid
 */
bool amux_get_frame(amux_frame_t* out_frame);

/**
 * @brief Advance or trigger an autonomous AMUX DMA sweep cycle
 * @return true if sweep started or completed successfully
 */
bool amux_dma_sweep_start(void);

/**
 * @brief Get coherent snapshot of all 16 AMUX channels from DMA buffer
 * @param out_raw Buffer of at least HW_ADC_MUX_COUNT entries
 * @param count Number of entries to copy
 * @return true if snapshot copied coherently, false on ADC contention
 */
bool amux_dma_get_coherent(uint16_t* out_raw, size_t count);

/**
 * @brief Get leased DMA channel numbers for diagnostics
 */
int32_t amux_dma_get_data_channel(void);
int32_t amux_dma_get_ctrl_channel(void);

/**
 * @brief Get currently owning core of the ADC lock
 * @return Core ID (0 or 1), or -1 if unowned
 */
int8_t adc_get_owner(void);

/**
 * @brief Get reentrant nesting count of current ADC lock
 * @return Nesting depth (0 if unowned)
 */
uint32_t adc_get_nest_count(void);

/**
 * @brief Flag to reset averaging
 * 
 * Set to true to reset averaging of all channels and start with current value.
 * Useful when expecting step-change of inputs.
 */
extern bool reset_adc_average;

/** Number of samples for averaging (power of 2 for efficient division) */
#define ADC_AVG_TIMES 64

/**
 * @brief Calculate average from sum
 * 
 * @param avgsum Sum of ADC_AVG_TIMES samples
 * @return Averaged value with correct rounding
 * 
 * @note Uses power-of-2 division for efficiency
 * @note Adds ADC_AVG_TIMES/2 before division for proper rounding
 */
inline uint32_t get_adc_average(uint32_t avgsum)
{
    // add ADC_AVG_TIMES/2 before division for correct rounding instead of cutting of decimals
    return ((avgsum+(ADC_AVG_TIMES/2))/ADC_AVG_TIMES);
}

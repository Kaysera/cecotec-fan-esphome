// Hub del componente: driver del SX1280 sobre `esphome::spi::SPIDevice`, el
// generador de trama por campos, y la tarea de transmision.
//
// Portado de el firmware previo de esta serie,
// que es el que consiguio que el ventilador obedezca. La secuencia de opcodes
// NO cambia; lo unico que cambia es que las transacciones van por SPIDevice en
// vez de por `SPI.h` de Arduino, para no quedarnos anclados al framework
// Arduino (el README).
//
// Procedencia de cada opcode/valor: datasheet Semtech SX1280 Rev 3.2,
// con RadioLib y ExpressLRS como referencia cruzada.
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/spi/spi.h"

#include "protocol.h"

#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#endif

namespace esphome {
namespace cecotec_ventilador {

// --- Opcodes (datasheet Rev 3.2, Tabla 11-1) ---
static const uint8_t SX_WRITE_REGISTER = 0x18;
static const uint8_t SX_READ_REGISTER = 0x19;
static const uint8_t SX_WRITE_BUFFER = 0x1A;
static const uint8_t SX_GET_IRQ_STATUS = 0x15;
static const uint8_t SX_SET_STANDBY = 0x80;
static const uint8_t SX_SET_TX = 0x83;
static const uint8_t SX_SET_RF_FREQUENCY = 0x86;
static const uint8_t SX_SET_PACKET_TYPE = 0x8A;
static const uint8_t SX_SET_MODULATION_PARAMS = 0x8B;
static const uint8_t SX_SET_PACKET_PARAMS = 0x8C;
static const uint8_t SX_SET_DIO_IRQ_PARAMS = 0x8D;
static const uint8_t SX_SET_TX_PARAMS = 0x8E;
static const uint8_t SX_SET_BUFFER_BASE_ADDRESS = 0x8F;
static const uint8_t SX_CLR_IRQ_STATUS = 0x97;

static const uint8_t SX_STANDBY_RC = 0x00;
static const uint8_t SX_PACKET_TYPE_GFSK = 0x00;
static const uint16_t SX_REG_SYNCWORD1 = 0x09CE;  // Tabla 14-11
static const uint16_t SX_IRQ_TX_DONE = 0x0001;
static const uint16_t SX_IRQ_RXTX_TIMEOUT = 0x4000;
static const double SX_XTAL_HZ = 52000000.0;
static const uint8_t SX_BUF_BASE_A = 0x00;
static const uint8_t SX_BUF_BASE_B = 0x80;

// Una pulsacion es la estructura real del mando, medida en el mando real: 6 bloques de 80 paquetes, periodo 1161 us,
// huecos de ~9 ms entre bloques, las dos subvariantes alternando de 3 en 3 con
// un contador global que NO se reinicia por bloque.
struct PressParams {
  uint16_t blocks = 6;
  uint16_t packets_per_block = 80;
  uint32_t interval_us = 1161;
  uint16_t group = 3;
  uint32_t block_gap_us = 9000;
};

// Lo que se encola desde `control()` de una entidad. Deliberadamente pequeño y
// trivialmente copiable: viaja por una cola de FreeRTOS.
struct CecotecRequest {
  // >= 0: se emite la pulsacion REAL capturada de ese boton (indice en
  // CECOTEC_BANK_*). < 0: se genera la trama por campos con family/param.
  int8_t cmd_index;
  uint8_t family;
  uint16_t param;
  uint8_t light_flag;
};

class CecotecHub : public Component,
                   public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                         spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_2MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_busy_pin(GPIOPin *pin) { this->busy_pin_ = pin; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_dry_run(bool dry_run) { this->dry_run_ = dry_run; }
  void set_frequency(uint32_t hz) { this->freq_hz_ = hz; }
  void set_power(int8_t dbm) { this->power_dbm_ = dbm; }
  void set_press(const PressParams &p) { this->press_ = p; }
  /// `true`: emitir las pulsaciones reales del banco (lo unico que el receptor
  /// acepta hoy, ver el README, seccion "El resultado negativo que cambio el diseño"). `false`: generar por campos, que es
  /// el objetivo de G1 pero todavia no lo obedece el ventilador.
  void set_use_bank(bool v) { this->use_bank_ = v; }
  bool use_bank() const { return this->use_bank_; }
  /// Valor inicial del contador de pulsacion. Sirve para dos cosas: marcar la
  /// procedencia de los bits en una iteracion de medida (GOAL.md: "contador de
  /// iteracion dentro del payload"), y evitar que dos arranques seguidos
  /// empiecen en el mismo valor, que es justo lo que el receptor deduplica.
  void set_counter_start(uint8_t c) {
    this->counter_ = c;
    this->counter_start_ = c;
  }
  /// Autoprueba: emite `fan_1` cada N ms sin que nadie lo pida. Existe para
  /// poder medir la emision **sin red**, que es la unica forma de atribuir el
  /// jitter del periodo al WiFi en vez de suponerlo. Ver el README.
  /// Cada pulsacion de autoprueba reusa el mismo contador, para que todas las
  /// rafagas de una captura se comparen contra la misma trama esperada.
  void set_test_press_interval(uint32_t ms) { this->test_interval_ms_ = ms; }

  /// Encola un comando. Se llama desde `control()` de las entidades y NO
  /// bloquea: una pulsacion dura ~613 ms y eso tumbaria el bucle de ESPHome.
  void send_command(uint8_t family, uint16_t param, uint8_t light_flag);
  /// Encola uno de los 14 botones del banco por nombre.
  bool send_button(const char *name);

  /// Construye los 40 bytes de `b6d` a partir de campos. Espejo exacto de
  /// el generador offline (fuera de este repo); la puerta de P4 compara las dos salidas.
  void build_b6d(uint8_t *out, uint8_t family, uint16_t param, uint8_t light_flag,
                 uint8_t counter) const;

  uint8_t counter() const { return this->counter_; }
  bool dry_run() const { return this->dry_run_; }
  /// Indice de un boton en la tabla, o -1. Lo usan las entidades para no
  /// duplicar la tabla de nombres.
  static int8_t command_index(const char *name);

 protected:
  // --- driver SX1280 ---
  bool wait_busy_(uint32_t timeout_us = 20000);
  void reset_pulse_();
  void cmd_write_(uint8_t opcode, const uint8_t *params, size_t n);
  void cmd_read_(uint8_t opcode, uint8_t *out, size_t n, size_t n_nop = 1);
  void write_register_(uint16_t addr, const uint8_t *data, size_t n);
  void write_buffer_(uint8_t offset, const uint8_t *data, size_t n);
  uint16_t get_irq_status_();
  void clear_irq_status_(uint16_t mask = 0xFFFF);
  void set_standby_(uint8_t mode = SX_STANDBY_RC);
  void set_rf_frequency_(uint32_t hz);
  void set_tx_params_(int8_t dbm, uint8_t ramp);
  void set_packet_params_(uint16_t preamble_bits, uint8_t sync_len_bytes, uint8_t payload_len);
  void set_buffer_base_address_(uint8_t tx, uint8_t rx);
  void set_tx_(uint8_t period_base, uint16_t period_count);
  bool configure_(bool do_reset);
  bool tx_one_(uint32_t timeout_us, uint16_t *irq);
  static uint32_t freq_to_reg_(uint32_t hz);
  static uint8_t preamble_reg_(uint16_t bits);

  // --- pulsacion ---
  void run_press_(const CecotecRequest &req);
  void enqueue_(const CecotecRequest &req);
  uint8_t build_press_(const CecotecRequest &req, uint8_t *a04, uint8_t *b6d, uint8_t counter);
  void log_frame_(const CecotecRequest &req, const uint8_t *a04, const uint8_t *b6d,
                  uint8_t counter, uint8_t session);

#ifdef USE_ESP32
  static void tx_task_trampoline_(void *arg);
  void tx_task_();
  QueueHandle_t queue_{nullptr};
  TaskHandle_t task_{nullptr};
#endif

  GPIOPin *busy_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  bool dry_run_{true};
  uint32_t freq_hz_{2402000000UL};
  int8_t power_dbm_{13};
  PressParams press_;

  // El contador de pulsacion del protocolo. Sube de uno en uno
  // en cada pulsacion y es lo que vence la deduplicacion del receptor.
  uint8_t counter_{0};
  uint8_t counter_start_{0};
  uint32_t test_interval_ms_{0};
  uint32_t last_test_ms_{0};
  bool use_bank_{true};
  // Pulsacion real que toca emitir para cada boton. Rotar entre las 3
  // capturadas es lo que vence la deduplicacion del receptor.
  uint8_t session_[CECOTEC_COMMAND_COUNT] = {};
  bool configured_{false};
  // Diagnostico de la ultima pulsacion, volcado en `loop()` desde el hilo de
  // ESPHome (no se loguea desde la tarea de TX: el timing es sensible).
  volatile bool press_done_{false};
  volatile uint32_t last_sent_{0}, last_txdone_{0}, last_total_us_{0};
  volatile uint32_t last_period_min_us_{0}, last_period_max_us_{0};
  // El contador que salio de verdad en la ultima pulsacion. Se asigna dentro
  // de la tarea de TX, asi que es la unica forma de saber desde fuera contra
  // que valor hay que verificar la captura.
  volatile uint8_t last_counter_{0};
  volatile int8_t last_cmd_{-1};
  volatile uint8_t last_session_{0};
  volatile uint8_t last_family_{0};
  volatile uint16_t last_param_{0};
};

}  // namespace cecotec_ventilador
}  // namespace esphome

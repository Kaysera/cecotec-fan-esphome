#include "cecotec_ventilador.h"

#include "frame_builder.h"

#include "esphome/core/log.h"

#include <cstring>

namespace esphome {
namespace cecotec_ventilador {

static const char *const TAG = "cecotec_ventilador";

// ---------------------------------------------------------------------------
// generador de trama
//
// La logica vive en frame_builder.h, sin dependencias de ESPHome, para poder
// compilarla en el PC y compararla contra la implementacion de referencia en
// Python. Aqui solo se reexpone como metodo del hub.
//
// Solo se usa con `use_bank: false`: el receptor no acepta tramas generadas.

void CecotecHub::build_b6d(uint8_t *out, uint8_t family, uint16_t param, uint8_t light_flag,
                           uint8_t counter) const {
  frame_build_b6d(out, family, param, light_flag, counter);
}

// ---------------------------------------------------------------------------
// ciclo de vida

void CecotecHub::setup() {
  if (this->busy_pin_ != nullptr) {
    this->busy_pin_->setup();
  }
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
  }

  if (this->dry_run_) {
    // el modo de prueba: las entidades existen y logean la trama, pero no se toca
    // el bus ni sale RF. Ni siquiera se inicializa el SPI.
    ESP_LOGCONFIG(TAG, "dry_run activo: no se configura el SX1280 ni sale RF");
    return;
  }

  this->spi_setup();
  this->configured_ = this->configure_(true);
  if (!this->configured_) {
    ESP_LOGE(TAG, "el SX1280 no responde: BUSY no bajo tras el reset");
    this->mark_failed();
    return;
  }

#ifdef USE_ESP32
  this->queue_ = xQueueCreate(8, sizeof(CecotecRequest));
  if (this->queue_ == nullptr) {
    ESP_LOGE(TAG, "no se pudo crear la cola de transmision");
    this->mark_failed();
    return;
  }
  // La rafaga va en su propia tarea clavada al core 1, con el WiFi en el core
  // 0 (riesgo 1 y 2 de el README). Prioridad alta porque el periodo de
  // 1161 us se mantiene con espera activa sobre micros().
  // Prioridad maxima. Medido en P1: con el WiFi asociado, el 10.6% de los
  // periodos se salia del +-3% (802-1567 us), y sin red el 0.0% (1155-1167).
  // El jitter es del WiFi, asi que se le quita cualquier tarea por delante.
  xTaskCreatePinnedToCore(&CecotecHub::tx_task_trampoline_, "cecotec_tx", 4096, this,
                          configMAX_PRIORITIES - 1, &this->task_, 1);
#endif
}

void CecotecHub::loop() {
  // Autoprueba sin red: ver set_test_press_interval(). El contador se
  // restablece en cada pulsacion para que toda la captura se pueda verificar
  // contra una unica trama esperada.
  if (this->test_interval_ms_ != 0) {
    const uint32_t now = millis();
    if (now - this->last_test_ms_ >= this->test_interval_ms_) {
      this->last_test_ms_ = now;
      this->counter_ = this->counter_start_;
      this->send_button("fan_1");
    }
  }

  if (this->press_done_) {
    this->press_done_ = false;
    // El contador va en el log porque es contra el que se verifica la captura:
    // los bits capturados solo dan 0 errores si llevan ESTE valor, y ese es el
    // argumento de procedencia de la medida.
    if (this->last_cmd_ >= 0) {
      ESP_LOGI(TAG, "pulsacion emitida: boton=%s, pulsacion %u de %u del banco "
                    "(familia=0x%02X param=0x%04X)",
               CECOTEC_COMMANDS[this->last_cmd_].name, (unsigned) this->last_session_,
               (unsigned) CECOTEC_BANK_SESSIONS, (unsigned) this->last_family_,
               (unsigned) this->last_param_);
    } else {
      ESP_LOGI(TAG, "pulsacion emitida: generada, familia=0x%02X param=0x%04X contador=%u",
               (unsigned) this->last_family_, (unsigned) this->last_param_,
               (unsigned) this->last_counter_);
    }
    ESP_LOGD(TAG, "pulsacion: %u/%u paquetes con TxDone, %u ms, periodo %u..%u us",
             (unsigned) this->last_txdone_, (unsigned) this->last_sent_,
             (unsigned) (this->last_total_us_ / 1000), (unsigned) this->last_period_min_us_,
             (unsigned) this->last_period_max_us_);
  }
}

void CecotecHub::dump_config() {
  ESP_LOGCONFIG(TAG, "Cecotec ventilador:");
  ESP_LOGCONFIG(TAG, "  dry_run: %s", YESNO(this->dry_run_));
  ESP_LOGCONFIG(TAG, "  frecuencia: %u Hz", (unsigned) this->freq_hz_);
  ESP_LOGCONFIG(TAG, "  potencia: %d dBm", this->power_dbm_);
  ESP_LOGCONFIG(TAG, "  pulsacion: %u bloques x %u paquetes, periodo %u us, grupo %u, hueco %u us",
                (unsigned) this->press_.blocks, (unsigned) this->press_.packets_per_block,
                (unsigned) this->press_.interval_us, (unsigned) this->press_.group,
                (unsigned) this->press_.block_gap_us);
  LOG_PIN("  BUSY: ", this->busy_pin_);
  LOG_PIN("  NRESET: ", this->reset_pin_);
  if (!this->dry_run_) {
    ESP_LOGCONFIG(TAG, "  SX1280 configurado: %s", YESNO(this->configured_));
  }
}

// ---------------------------------------------------------------------------
// encolado

int8_t CecotecHub::command_index(const char *name) {
  for (size_t i = 0; i < CECOTEC_COMMAND_COUNT; i++) {
    if (strcmp(CECOTEC_COMMANDS[i].name, name) == 0) {
      return (int8_t) i;
    }
  }
  return -1;
}

void CecotecHub::enqueue_(const CecotecRequest &req) {
  if (this->dry_run_) {
    uint8_t a04[PAYLOAD_BYTES], b6d[PAYLOAD_BYTES];
    uint8_t counter = this->counter_;
    uint8_t session = this->build_press_(req, a04, b6d, counter);
    this->counter_++;
    this->log_frame_(req, a04, b6d, counter, session);
    return;
  }

#ifdef USE_ESP32
  if (this->queue_ == nullptr) {
    ESP_LOGW(TAG, "cola no inicializada, se descarta el comando");
    return;
  }
  if (xQueueSend(this->queue_, &req, 0) != pdTRUE) {
    ESP_LOGW(TAG, "cola llena, se descarta el comando (familia 0x%02X param 0x%04X)", req.family,
             req.param);
  }
#else
  this->run_press_(req);
#endif
}

void CecotecHub::send_command(uint8_t family, uint16_t param, uint8_t light_flag) {
  // Ruta generada por campos: solo para valores que NO estan en el banco (por
  // ejemplo un nivel de luz intermedio). Hoy el receptor no la acepta, ver
  // el README, seccion "El resultado negativo que cambio el diseño".
  CecotecRequest req{-1, family, param, light_flag};
  this->enqueue_(req);
}

bool CecotecHub::send_button(const char *name) {
  int8_t idx = CecotecHub::command_index(name);
  if (idx < 0) {
    ESP_LOGW(TAG, "boton desconocido: %s", name);
    return false;
  }
  const CecotecCommand &cmd = CECOTEC_COMMANDS[idx];
  // En modo banco se emite la pulsacion REAL capturada de ese boton; en modo
  // generado, sus campos sobre la plantilla.
  CecotecRequest req{this->use_bank_ ? idx : (int8_t) -1, cmd.family, cmd.param, cmd.light_flag};
  this->enqueue_(req);
  return true;
}

/// Deja en `a04`/`b6d` los 40+40 bytes que se van a emitir. Devuelve la
/// pulsacion del banco usada (0 si se genero por campos).
uint8_t CecotecHub::build_press_(const CecotecRequest &req, uint8_t *a04, uint8_t *b6d,
                                 uint8_t counter) {
  if (req.cmd_index >= 0) {
    // Las tres pulsaciones capturadas de este boton se rotan. Van tal cual,
    // SIN parchear el contador: son tramas reales del mando y cualquier
    // retoque de los bits que no entendemos las invalida.
    uint8_t s = this->session_[req.cmd_index];
    this->session_[req.cmd_index] = (uint8_t) ((s + 1) % CECOTEC_BANK_SESSIONS);
    memcpy(a04, CECOTEC_BANK_A04[req.cmd_index][s], PAYLOAD_BYTES);
    memcpy(b6d, CECOTEC_BANK_B6D[req.cmd_index][s], PAYLOAD_BYTES);
    return s;
  }
  memcpy(a04, CECOTEC_TEMPLATE_A04, PAYLOAD_BYTES);
  this->build_b6d(b6d, req.family, req.param, req.light_flag, counter);
  return 0;
}

void CecotecHub::log_frame_(const CecotecRequest &req, const uint8_t *a04, const uint8_t *b6d,
                            uint8_t counter, uint8_t session) {
  char ha[2 * PAYLOAD_BYTES + 1], hb[2 * PAYLOAD_BYTES + 1];
  for (size_t i = 0; i < PAYLOAD_BYTES; i++) {
    sprintf(ha + 2 * i, "%02x", a04[i]);
    sprintf(hb + 2 * i, "%02x", b6d[i]);
  }
  // Que quede MUY claro que no ha salido nada por la antena: este log solo
  // ocurre en dry_run, y sin decirlo parece una emision normal. Es justo el
  // fallo silencioso que hizo perder tiempo dos veces.
  ESP_LOGW(TAG, "DRY RUN: la trama NO se emite. Quita `dry_run: true` del YAML.");
  if (req.cmd_index >= 0) {
    ESP_LOGI(TAG, "boton=%s (banco, pulsacion %u de %u) familia=0x%02X param=0x%04X",
             CECOTEC_COMMANDS[req.cmd_index].name, (unsigned) session,
             (unsigned) CECOTEC_BANK_SESSIONS, req.family, req.param);
  } else {
    ESP_LOGI(TAG, "generada por campos: familia=0x%02X param=0x%04X luz=%u contador=%u",
             req.family, req.param, req.light_flag, counter);
  }
  ESP_LOGI(TAG, "  a04: %s", ha);
  ESP_LOGI(TAG, "  b6d: %s", hb);
}

// ---------------------------------------------------------------------------
// tarea de transmision

#ifdef USE_ESP32
void CecotecHub::tx_task_trampoline_(void *arg) { static_cast<CecotecHub *>(arg)->tx_task_(); }

void CecotecHub::tx_task_() {
  CecotecRequest req;
  for (;;) {
    if (xQueueReceive(this->queue_, &req, portMAX_DELAY) == pdTRUE) {
      this->run_press_(req);
    }
  }
}
#endif

void CecotecHub::run_press_(const CecotecRequest &req) {
  uint8_t a04[PAYLOAD_BYTES], b6d[PAYLOAD_BYTES];
  uint8_t counter = this->counter_++;
  uint8_t session = this->build_press_(req, a04, b6d, counter);
  this->last_counter_ = counter;
  this->last_family_ = req.family;
  this->last_param_ = req.param;
  this->last_cmd_ = req.cmd_index;
  this->last_session_ = session;

  // El payload que come el chip son 45 bytes: el sync word real del mando va
  // dentro (ver protocol.h), porque el PreambleLength del SX1280 satura en 32
  // bits y el preambulo real son 62.
  uint8_t pkt_a[5 + PAYLOAD_BYTES];
  uint8_t pkt_b[5 + PAYLOAD_BYTES];
  memcpy(pkt_a, CECOTEC_SYNC, 5);
  memcpy(pkt_a + 5, a04, PAYLOAD_BYTES);
  memcpy(pkt_b, CECOTEC_SYNC, 5);
  memcpy(pkt_b + 5, b6d, PAYLOAD_BYTES);

  const uint8_t len = 5 + PAYLOAD_BYTES;
  this->configure_(true);
  this->set_packet_params_(CECOTEC_CHIP_PREAMBLE_BITS, 5, len);
  this->write_buffer_(SX_BUF_BASE_A, pkt_a, len);
  this->write_buffer_(SX_BUF_BASE_B, pkt_b, len);
  this->wait_busy_();

  // El primer SetBufferBaseAddress va FUERA del bucle: dentro retrasaba solo al
  // paquete 0 y el primer periodo salia ~38 us corto, el unico de los 79 que se
  // salia del +-3% de la puerta (una medida previa de esta serie).
  this->set_buffer_base_address_(SX_BUF_BASE_A, 0x00);
  this->wait_busy_();
  uint8_t cur_base = SX_BUF_BASE_A;

  uint32_t sent = 0, txdone = 0, seq = 0;
  uint32_t prev = 0, pmin = 0xFFFFFFFF, pmax = 0;
  const uint32_t t_start = micros();

  for (uint16_t blk = 0; blk < this->press_.blocks; blk++) {
    uint32_t t_blk = t_start + blk * (this->press_.packets_per_block * this->press_.interval_us +
                                      this->press_.block_gap_us);
    for (uint16_t i = 0; i < this->press_.packets_per_block; i++, seq++) {
      // Alternancia de 3 en 3 con contador GLOBAL, no por bloque: el del mando
      // es libre y no se reinicia al empezar un bloque.
      bool is_a = ((seq / this->press_.group) % 2) == 0;
      uint8_t base = is_a ? SX_BUF_BASE_A : SX_BUF_BASE_B;
      if (base != cur_base) {
        this->set_buffer_base_address_(base, 0x00);
        cur_base = base;
      }
      uint32_t target = t_blk + i * this->press_.interval_us;
      while ((int32_t) (micros() - target) < 0) {
      }
      uint32_t t_now = micros();
      uint16_t irq;
      if (this->tx_one_(2000, &irq)) {
        txdone++;
      }
      sent++;
      if (prev != 0 && i != 0) {
        uint32_t d = t_now - prev;
        if (d < pmin)
          pmin = d;
        if (d > pmax)
          pmax = d;
      }
      prev = t_now;
    }
  }

  this->last_total_us_ = micros() - t_start;
  this->last_sent_ = sent;
  this->last_txdone_ = txdone;
  this->last_period_min_us_ = (pmin == 0xFFFFFFFF) ? 0 : pmin;
  this->last_period_max_us_ = pmax;
  this->press_done_ = true;
  this->set_standby_();
}

// ---------------------------------------------------------------------------
// driver SX1280
//
// Secuencia de opcodes validada contra el mando real; solo cambian las transacciones.

bool CecotecHub::wait_busy_(uint32_t timeout_us) {
  if (this->busy_pin_ == nullptr)
    return true;
  uint32_t t0 = micros();
  while ((uint32_t) (micros() - t0) < timeout_us) {
    if (!this->busy_pin_->digital_read())
      return true;
  }
  return false;
}

void CecotecHub::reset_pulse_() {
  if (this->reset_pin_ == nullptr)
    return;
  // Margenes de ExpressLRS (50 ms bajo + 50 ms tras soltarlo) para no sondear
  // BUSY antes de que el chip lo haya subido (una medida previa).
  this->reset_pin_->digital_write(true);
  delayMicroseconds(1000);
  this->reset_pin_->digital_write(false);
  delay(50);
  this->reset_pin_->digital_write(true);
  delay(50);
}

void CecotecHub::cmd_write_(uint8_t opcode, const uint8_t *params, size_t n) {
  this->wait_busy_();
  this->enable();
  this->write_byte(opcode);
  if (n != 0)
    this->write_array(params, n);
  this->disable();
}

void CecotecHub::cmd_read_(uint8_t opcode, uint8_t *out, size_t n, size_t n_nop) {
  this->wait_busy_();
  this->enable();
  this->write_byte(opcode);
  for (size_t i = 0; i < n_nop; i++)
    this->transfer_byte(0x00);
  for (size_t i = 0; i < n; i++)
    out[i] = this->transfer_byte(0x00);
  this->disable();
}

void CecotecHub::write_register_(uint16_t addr, const uint8_t *data, size_t n) {
  this->wait_busy_();
  this->enable();
  this->write_byte(SX_WRITE_REGISTER);
  this->write_byte((addr >> 8) & 0xFF);
  this->write_byte(addr & 0xFF);
  this->write_array(data, n);
  this->disable();
}

void CecotecHub::write_buffer_(uint8_t offset, const uint8_t *data, size_t n) {
  this->wait_busy_();
  this->enable();
  this->write_byte(SX_WRITE_BUFFER);
  this->write_byte(offset);
  this->write_array(data, n);
  this->disable();
}

uint16_t CecotecHub::get_irq_status_() {
  uint8_t b[2];
  this->cmd_read_(SX_GET_IRQ_STATUS, b, 2, 1);
  return ((uint16_t) b[0] << 8) | b[1];
}

void CecotecHub::clear_irq_status_(uint16_t mask) {
  uint8_t p[2] = {(uint8_t) (mask >> 8), (uint8_t) (mask & 0xFF)};
  this->cmd_write_(SX_CLR_IRQ_STATUS, p, 2);
}

void CecotecHub::set_standby_(uint8_t mode) { this->cmd_write_(SX_SET_STANDBY, &mode, 1); }

uint32_t CecotecHub::freq_to_reg_(uint32_t hz) {
  // Frf = round(f * 2^18 / Fxtal), Fxtal = 52 MHz
  double v = ((double) hz * 262144.0) / SX_XTAL_HZ;
  return (uint32_t) (v + 0.5);
}

void CecotecHub::set_rf_frequency_(uint32_t hz) {
  uint32_t r = freq_to_reg_(hz);
  uint8_t p[3] = {(uint8_t) ((r >> 16) & 0xFF), (uint8_t) ((r >> 8) & 0xFF), (uint8_t) (r & 0xFF)};
  this->cmd_write_(SX_SET_RF_FREQUENCY, p, 3);
}

void CecotecHub::set_buffer_base_address_(uint8_t tx, uint8_t rx) {
  uint8_t p[2] = {tx, rx};
  this->cmd_write_(SX_SET_BUFFER_BASE_ADDRESS, p, 2);
}

void CecotecHub::set_tx_params_(int8_t dbm, uint8_t ramp) {
  // power_reg = dBm + 18. IMPRESCINDIBLE: sin SetTxParams el PA nunca queda
  // configurado y no sale RF aunque TxDone se confirme (bug visto en una version previa).
  if (dbm < -18)
    dbm = -18;
  if (dbm > 13)
    dbm = 13;
  uint8_t p[2] = {(uint8_t) (dbm + 18), ramp};
  this->cmd_write_(SX_SET_TX_PARAMS, p, 2);
}

uint8_t CecotecHub::preamble_reg_(uint16_t bits) {
  if (bits < 4)
    bits = 4;
  if (bits > 32)
    bits = 32;
  uint8_t n = bits / 4;
  if (n < 1)
    n = 1;
  return (uint8_t) ((n - 1) << 4);
}

void CecotecHub::set_packet_params_(uint16_t preamble_bits, uint8_t sync_len_bytes,
                                    uint8_t payload_len) {
  uint8_t p[7];
  p[0] = preamble_reg_(preamble_bits);
  p[1] = (uint8_t) ((sync_len_bytes - 1) * 2);  // 5 bytes -> 0x08
  p[2] = 0x10;                                  // SyncWordMatch = sync word 1
  p[3] = 0x00;                                  // HeaderType = fixed length
  p[4] = payload_len;
  p[5] = 0x00;  // CRC off
  p[6] = 0x08;  // Whitening off
  this->cmd_write_(SX_SET_PACKET_PARAMS, p, 7);
}

void CecotecHub::set_tx_(uint8_t period_base, uint16_t period_count) {
  uint8_t p[3] = {period_base, (uint8_t) (period_count >> 8), (uint8_t) (period_count & 0xFF)};
  this->cmd_write_(SX_SET_TX, p, 3);
}

bool CecotecHub::configure_(bool do_reset) {
  if (do_reset) {
    this->reset_pulse_();
    if (!this->wait_busy_(50000))
      return false;
  }
  this->set_standby_(SX_STANDBY_RC);
  uint8_t t = SX_PACKET_TYPE_GFSK;
  this->cmd_write_(SX_SET_PACKET_TYPE, &t, 1);
  this->set_rf_frequency_(this->freq_hz_);
  this->set_buffer_base_address_(SX_BUF_BASE_A, 0x00);
  // 1.000 Mb/s BW 2.4 MHz, beta 0.35 (deviation ~175 kHz nominal, medida
  // 157-167 kHz medidos), BT_0_5.
  uint8_t mod[3] = {0x4C, 0x00, 0x20};
  this->cmd_write_(SX_SET_MODULATION_PARAMS, mod, 3);
  this->set_tx_params_(this->power_dbm_, 0x00);
  this->set_packet_params_(CECOTEC_CHIP_PREAMBLE_BITS, 5, 5 + PAYLOAD_BYTES);
  this->write_register_(SX_REG_SYNCWORD1, CECOTEC_CHIP_SYNC, 5);
  uint8_t irq[8] = {(uint8_t) ((SX_IRQ_TX_DONE | SX_IRQ_RXTX_TIMEOUT) >> 8),
                    (uint8_t) ((SX_IRQ_TX_DONE | SX_IRQ_RXTX_TIMEOUT) & 0xFF),
                    0, 0, 0, 0, 0, 0};
  this->cmd_write_(SX_SET_DIO_IRQ_PARAMS, irq, 8);
  this->clear_irq_status_(0xFFFF);
  return this->wait_busy_();
}

bool CecotecHub::tx_one_(uint32_t timeout_us, uint16_t *irq) {
  this->clear_irq_status_(0xFFFF);
  this->set_tx_(0x02, 50);  // periodBase 1 ms, count 50 -> timeout 50 ms
  uint32_t t0 = micros();
  while ((uint32_t) (micros() - t0) < timeout_us) {
    uint16_t s = this->get_irq_status_();
    if (s & (SX_IRQ_TX_DONE | SX_IRQ_RXTX_TIMEOUT)) {
      *irq = s;
      return (s & SX_IRQ_TX_DONE) != 0;
    }
  }
  *irq = 0;
  return false;
}

}  // namespace cecotec_ventilador
}  // namespace esphome

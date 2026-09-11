# Cecotec Fan ESPHome component

Componente externo de [ESPHome](https://esphome.io) para controlar un
**ventilador de techo Cecotec con luz** desde Home Assistant, sustituyendo al
mando a distancia por radio.

El mando habla por **radio propietaria en
2.4 GHz**, así que el protocolo se sacó por ingeniería inversa con un SDR y se
reimplementó sobre un ESP32 con un transceptor SX1280.

> **Disclaimer:** La ingeniería inversa del protocolo y captura de las tramas se han hecho
> manualmente usando URH, y el estudio de las distintas partes de la señal han sido
> ayudadas por IA. El código del componente sí está realizado totalmente por IA, sólo
> verificado empíricamente. Este repo sirve principalmente como inspiración para
> adaptar y automatizar electrodomésticos más opacos donde el fabricante
> no ayuda ni proporciona ningún tipo de documentación; ya que es muy posible que
> solo sirva para un modelo Cecotec concreto. El README también está escrito por LLM y
> retocado a mano después.

---

## Qué te da en Home Assistant

Una entidad `fan` y cinco `button` que replican los botones del mando:

| Entidad | Tipo | Qué hace |
|---|---|---|
| Ventilador | `fan` | On/off, **6 velocidades**, **dirección** = modo frío/calor, **preset "Brisa"** |
| Luz | `button` | Alterna la luz (es un toggle, como en el mando) |
| Luz fría / neutra / cálida | `button` | Fija la temperatura de color |
| Apagado general | `button` | Apaga el aparato entero, luz incluida |

---

## Hardware

| Pieza | Para qué |
|---|---|
| **LilyGo T3-S3** (ESP32-S3 + **Semtech SX1280**) | El transmisor. El SX1280 es de los pocos transceptores accesibles que hacen GFSK a 1 Mb/s en 2.4 GHz con control del framing. El LilyGo viene ya con el ESP32 soldado, porque los pines son más finos que los cables Dupont y hace que soldar cables a un ESP32 comprando solo el transceptor sea muy dificil. |
| **HackRF One** | Solo para la ingeniería inversa y la verificación. **No hace falta para usar el componente.** |
| Ventilador de techo Cecotec con mando de radio 2.4 GHz | El objetivo. |

El pinout es el oficial de la T3-S3:

| Señal | GPIO |
|---|---|
| SCK | 5 |
| MISO | 3 |
| MOSI | 6 |
| NSS (CS) | 7 |
| NRESET | 8 |
| BUSY | 36 |

Sirve cualquier placa ESP32 con un SX1280 conectado por SPI; solo hay que
ajustar los pines.

> **Compatibilidad:** el mando no lleva marca identificable y Cecotec revende
> hardware genérico, así que es muy probable que otros ventiladores de radio a
> 2.4 GHz usen el mismo PCBA. Pero **los comandos de este repo son capturas de
> un mando concreto** y casi con seguridad no valdrán tal cual
> para otra unidad.

---

## Instalación

```yaml
external_components:
  - source: github://Kaysera/cecotec-fan-esphome
    components: [cecotec_ventilador]
```

ESPHome encuentra solo la carpeta `components/` de la raíz del repo.

### Configuración completa de ejemplo

Esta configuración es **completa y se puede copiar tal cual**: incluye `wifi`,
`api` y `ota`, sin los cuales el dispositivo no aparece en Home Assistant. Los
valores sensibles van por `!secret`, que es una referencia a tu `secrets.yaml`
y no contiene ninguna credencial.

Necesita estas cuatro claves en tu `secrets.yaml` (en el Device Builder es la
pestaña **Secrets**):

```yaml
wifi_ssid: "TU_RED_WIFI"
wifi_password: "TU_CONTRASENA_WIFI"
ota_password: "la_que_quieras"
# 32 bytes en base64. El Device Builder la genera sola al crear un dispositivo:
#   openssl rand -base64 32
api_encryption_key: "GENERA_LA_TUYA="
```

```yaml
esphome:
  name: cecotec-ventilador

esp32:
  board: esp32s3box
  framework:
    type: esp-idf

external_components:
  - source: github://Kaysera/cecotec-fan-esphome
    components: [cecotec_ventilador]

logger:
  # En la LilyGo T3-S3 el puerto serie es el USB-Serial/JTAG nativo del ESP32-S3.
  hardware_uart: USB_SERIAL_JTAG

api:
  encryption:
    key: !secret api_encryption_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  # El SX1280 emite en 2402 MHz y el WiFi esta en la misma banda y en la misma
  # placa. Esto evita que la radio WiFi despierte en mitad de una rafaga.
  power_save_mode: none

spi:
  clk_pin: GPIO5
  miso_pin: GPIO3
  mosi_pin: GPIO6

cecotec_ventilador:
  id: cecotec
  cs_pin: GPIO7
  busy_pin: GPIO36
  reset_pin: GPIO8

fan:
  - platform: cecotec_ventilador
    cecotec_ventilador_id: cecotec
    name: Ventilador

button:
  - platform: cecotec_ventilador
    cecotec_ventilador_id: cecotec
    command: light_toggle
    name: Luz
  - platform: cecotec_ventilador
    cecotec_ventilador_id: cecotec
    command: light_cold
    name: Luz fria
  - platform: cecotec_ventilador
    cecotec_ventilador_id: cecotec
    command: light_neutral
    name: Luz neutra
  - platform: cecotec_ventilador
    cecotec_ventilador_id: cecotec
    command: light_warm
    name: Luz calida
  - platform: cecotec_ventilador
    cecotec_ventilador_id: cecotec
    command: power_off
    name: Apagado general
```

### Opciones del hub

| Opción | Por defecto | Para qué |
|---|---|---|
| `cs_pin`, `busy_pin`, `reset_pin` | — | Pines del SX1280. |
| `dry_run` | `false` | Si es `true`, las entidades funcionan y la trama se escribe en el log, **pero no sale nada por radio**. Útil para depurar sin molestar a nadie. |
| `use_bank` | `true` | Emitir las pulsaciones reales capturadas. Ponerlo a `false` genera la trama por campos, **y hoy el receptor la rechaza** (ver abajo). |
| `frequency` | `2402MHz` | Canal. |
| `power` | `13` | dBm. |
| `blocks`, `packets_per_block`, `interval`, `group`, `block_gap` | 6, 80, 1161us, 3, 9000us | Estructura de una pulsación. Los valores por defecto son los medidos al mando real. |

Los `command` válidos son los 14 botones del mando: `fan_1`…`fan_6`, `fan_off`,
`breeze`, `cold_warm`, `light_toggle`, `light_cold`, `light_neutral`,
`light_warm`, `power_off`.

---

## Write-up del protocolo

Lo que se midió del mando real:

- **2402 MHz, GFSK, 1 Mb/s**, desviación **157-167 kHz**.
- Una pulsación son **6 bloques de 80 paquetes**, periodo **1161 µs**, con
  huecos de **~9 ms** entre bloques: **~613 ms** en total. El mando manda casi
  500 paquetes redundantes porque cuenta con perder muchos.
- Hay **dos subtramas que alternan de tres en tres**, llamadas aquí `a04` y
  `b6d` por su primer byte. El contador que decide cuál toca es **global a la
  pulsación**, no se reinicia en cada bloque.
- En el aire cada paquete es:

```
[preámbulo 24 b][sync word aaaaaaaad6][payload de 45 B = fb22e3e7de + 40 B]
```

Un detalle en cuanto a una limitación del chip: el preámbulo real del mando son **62 bits**,
pero el campo `PreambleLength` del SX1280 **satura en 32**. La solución es
repartirlo: los últimos 40 bits del preámbulo se cargan **como sync word del
chip**, y el sync word real del mando (`fb22e3e7de`) pasa a ser la cabecera del
payload. Con eso el ventilador obedece siempre.

Dentro de `b6d` se identificaron estos campos (bit 0 = primer bit del sync):

| Campo | Bits | Contenido |
|---|---|---|
| Familia (opcode) | 208-214 | 7 bits, el "tipo" de comando |
| Parámetro | 231-246 | 16 bits (p. ej. velocidad, o los dos canales de luz) |
| Contador de pulsación | 255-262 | 8 bits, sube de uno en uno. Sirve para deduplicar la señal. |
| Flag de luz | 264 | 1 si la trama trae niveles de canal de luz |
| **Sin explicar** | 271-318 y 335-359 | 73 bits que cambian en cada pulsación |

Y `a04` no lleva ningún campo de comando: sus primeros 159 bits son idénticos en
los 14 botones y los otros 201 **cambian en cada pulsación**.

El contador y los otros bits que cambian sirven porque como el mando envía
muchas veces la misma información (dado que cuenta con pérdidas), eso haría que
el ventilador reaccionase muchas veces. Entonces, con esta parte pseudo-aleatoria, 
es capaz de deduplicar la señal. Esto añade la complicación de que si se hace 
un replay-attack estándar no funciona, hay que alternarlo mínimo entre 2 variantes
de la pulsación (o generación) del botón para que lo pille siempre.

---

## Cómo se hizo la ingeniería inversa

El trabajo fue una serie de experimentos, cada uno con su hipótesis escrita
**antes** de medir y su criterio de fallo.

**1. Encontrar y caracterizar la señal.** Barrido con el HackRF hasta dar con
las ráfagas en 2402 MHz al pulsar el mando. El análisis por envolvente no
mostraba estructura: la pista fue mirar la **frecuencia instantánea**, que
reveló modulación angular (GFSK).

**2. Demodular y construir un banco de paquetes.** De las capturas IQ se
extrajeron los bits y se montó un banco de **14 botones × 3 pulsaciones = 42
sesiones**.

**3. Separar antes de promediar.** Al votar por mayoría entre los paquetes de
una pulsación salía un campo aparentemente aleatorio de alta entropía. Era un
artefacto: se estaban **mezclando las dos subtramas**. Separadas por subtipo,
cada paquete resulta ser **100% determinista** dentro de su pulsación. La separación
e identificación de las tramas fue la parte más manual del experimento. Usando URH
se identifica que las tramas tienen estructuras muy parecidas. Aplicando un 
filtro que permite la sincronización de los envíos (el preámbulo del mando no
era siempre igual) permite ver claramente cuales son las subtramas.

**4. Diferenciar campos.** Clasificando bit a bit las 42 sesiones en CONST
(igual en todas), BOTÓN (constante dentro de un botón) y VAR (cambia entre
pulsaciones del mismo botón) salen la familia, el parámetro y el flag de luz.

**5. Transmitir y medir** Cada emisión se captura con el
HackRF y se verifica **bit a bit** contra lo que se esperaba. Las reglas que
hicieron fiable el resultado:

- El HackRF **solo recibe**. Nunca se transmite con él.
- **Control negativo obligatorio** en cada medida: la misma captura con el chip
  en reposo tiene que dar 0 ráfagas.
- **Nunca validar contra una captura preexistente**: cada iteración crea la
  suya y el verificador comprueba la fecha del fichero.
- Una sola variable por iteración.
- El aprobado lo decide una herramienta, no la prosa de quien mide.

El uso del HackRF ayudó en gran medida a que se pudiera escribir con el chip
y leer con el HackRF de manera manual, mediante scripts y mediante LLMs, automatizando
y acelerando en gran medida el proceso de experimentación. Sin ser capaz de leer
las tramas que se envían desde el ESP32, hubiera sido casi imposible.

**6. Coexistencia con el WiFi.** El SX1280 emite en 2402 MHz y el WiFi del
ESP32-S3 está en la misma banda y en la misma placa, así que había que medirlo.
Con el WiFi asociado y cursando tráfico, **en un canal que contiene a 2402 MHz**
(el canal 1 ocupa 2401-2423), la emisión sale **bit-exacta al 100%** (250 de 250
ráfagas). Lo que sí aparece es **jitter de temporización**, y comparando contra
una compilación sin red quedó atribuido: con WiFi, 26 de 246 periodos fuera de
±3%; **sin red, 0 de 170**. Subir la prioridad de la tarea de transmisión lo baja
al 0.8%. El ventilador obedece igual, porque cada paquete es individualmente
correcto.

---

## Créditos y licencia

La ingeniería inversa, el firmware y el componente son trabajo propio sobre
hardware comprado. Este repo no contiene firmware, claves ni material del
fabricante: solo las tramas capturadas del aire de un mando propio, que es lo
que permite reproducir sus pulsaciones. Todos los experimentos se han llevado
a cabo en un entorno controlado. 

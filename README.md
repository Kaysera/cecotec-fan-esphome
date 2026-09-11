# cecotec-fan-esphome

Componente externo de [ESPHome](https://esphome.io) para controlar un
**ventilador de techo Cecotec con luz** desde Home Assistant, sustituyendo al
mando a distancia por radio.

No hay integración oficial ni nube: el mando habla por **radio propietaria en
2.4 GHz**, así que el protocolo se sacó por ingeniería inversa con un SDR y se
reimplementó sobre un ESP32 con un transceptor SX1280.

> **Estado:** funcionando y en uso diario. El ventilador obedece encendido,
> apagado, las 6 velocidades, el modo brisa, el modo frío/calor y los cuatro
> comandos de luz.

---

## Qué te da en Home Assistant

Una entidad `fan` y cinco `button`:

| Entidad | Tipo | Qué hace |
|---|---|---|
| Ventilador | `fan` | On/off, **6 velocidades**, **dirección** = modo frío/calor, **preset "Brisa"** |
| Luz | `button` | Alterna la luz (es un toggle, como en el mando) |
| Luz fría / neutra / cálida | `button` | Fija la temperatura de color |
| Apagado general | `button` | Apaga el aparato entero, luz incluida |

### Por qué la luz son botones y no una entidad `light`

**El mando nunca informa del estado del aparato.** Es un enlace unidireccional:
se manda y se reza. Una entidad `light` tiene estado (encendida/apagada, color),
y en cuanto alguien coja el mando físico ese estado pasa a ser mentira sin que
Home Assistant pueda enterarse. Un `button` no promete un estado que no se puede
conocer, así que es lo honesto.

Lo mismo vale para el ventilador: su estado es **optimista**. Se muestra lo
último que se ordenó, no lo que el aparato está haciendo. La forma de arreglarlo
de verdad sería poner el SX1280 en recepción para escuchar el mando físico y
sincronizar; no está hecho.

### Por qué las velocidades son escalones y no presets

Porque es lo que dice el propio código de ESPHome: `Fan::apply_preset_mode_()`
implementa la convención de Home Assistant de que **poner una velocidad borra el
preset**. Los presets son para *modos con nombre*, no para numerar velocidades.
Así que las 6 velocidades van como `set_supported_speed_count(6)`, y el preset
se reserva para lo que sí es un modo: **Brisa**.

El modo frío/calor va como **dirección de giro** (`FanDirection`), que es el
único modo de dos estados que ofrece el building block `fan`.

---

## Hardware

| Pieza | Para qué |
|---|---|
| **LilyGo T3-S3** (ESP32-S3 + **Semtech SX1280**) | El transmisor. El SX1280 es de los pocos transceptores accesibles que hacen GFSK a 1 Mb/s en 2.4 GHz con control del framing. |
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
> un mando concreto** (ver más abajo) y casi con seguridad no valdrán tal cual
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

Rellena `wifi`, `api` y `ota` como en cualquier dispositivo ESPHome; aquí se
omiten a propósito para no publicar credenciales.

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
| `dry_run` | `false` | Si es `true`, las entidades funcionan y la trama se escribe en el log, **pero no sale nada por radio**. Útil para probar sin molestar. |
| `use_bank` | `true` | Emitir las pulsaciones reales capturadas. Ponerlo a `false` genera la trama por campos, **y hoy el receptor la rechaza** (ver abajo). |
| `frequency` | `2402MHz` | Canal. |
| `power` | `13` | dBm. |
| `blocks`, `packets_per_block`, `interval`, `group`, `block_gap` | 6, 80, 1161us, 3, 9000us | Estructura de una pulsación. Los valores por defecto son los medidos al mando real. |

Los `command` válidos son los 14 botones del mando: `fan_1`…`fan_6`, `fan_off`,
`breeze`, `cold_warm`, `light_toggle`, `light_cold`, `light_neutral`,
`light_warm`, `power_off`.

---

## El protocolo

Lo que se midió del mando real:

- **2402 MHz, GFSK, 1 Mb/s**, deviation **157-167 kHz**.
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

El detalle que costó encontrar: el preámbulo real del mando son **62 bits**,
pero el campo `PreambleLength` del SX1280 **satura en 32**. La solución es
repartirlo: los últimos 40 bits del preámbulo se cargan **como sync word del
chip**, y el sync word real del mando (`fb22e3e7de`) pasa a ser la cabecera del
payload. Con eso el ventilador obedece siempre.

Dentro de `b6d` se identificaron estos campos (bit 0 = primer bit del sync):

| Campo | Bits | Contenido |
|---|---|---|
| Familia (opcode) | 208-214 | 7 bits, el "tipo" de comando |
| Parámetro | 231-246 | 16 bits (p. ej. velocidad, o los dos canales de luz) |
| Contador de pulsación | 255-262 | 8 bits, sube de uno en uno |
| Flag de luz | 264 | 1 si la trama trae niveles de canal de luz |
| **Sin explicar** | 271-318 y 335-359 | 73 bits que cambian en cada pulsación |

Y `a04` no lleva ningún campo de comando: sus primeros 159 bits son idénticos en
los 14 botones y los otros 201 **cambian en cada pulsación**.

---

## Cómo se hizo la ingeniería inversa

El trabajo fue una serie de experimentos, cada uno con su hipótesis escrita
**antes** de medir y su criterio de fallo.

**1. Encontrar y caracterizar la señal.** Barrido con el HackRF hasta dar con
las ráfagas en 2402 MHz al pulsar el mando. El análisis por envolvente no
mostraba estructura: la pista fue mirar la **frecuencia instantánea**, que
reveló modulación angular (GFSK) en vez de la OOK que se suponía.

**2. Demodular y construir un banco de paquetes.** De las capturas IQ se
extrajeron los bits y se montó un banco de **14 botones × 3 pulsaciones = 42
sesiones**.

**3. Separar antes de promediar.** Al votar por mayoría entre los paquetes de
una pulsación salía un campo aparentemente aleatorio de alta entropía. Era un
artefacto: se estaban **mezclando las dos subtramas**. Separadas por subtipo,
cada paquete resulta ser **100% determinista** dentro de su pulsación.

**4. Diferenciar campos.** Clasificando bit a bit las 42 sesiones en CONST
(igual en todas), BOTÓN (constante dentro de un botón) y VAR (cambia entre
pulsaciones del mismo botón) salen la familia, el parámetro y el flag de luz.

**5. Probar el contador sin razonar en círculo.** La primera "prueba" de que un
campo era un contador ordenaba las sesiones **por ese mismo campo** y observaba
que salían ordenadas. Eso no prueba nada. La prueba buena usó una captura con
**31 pulsaciones del mismo botón**, donde el orden es intrínseco al fichero:
valores 3, 4, 5 … 33, **30 incrementos de +1 sin una excepción**. De paso reveló
que el contador es de **8 bits y no de 6**, porque se vio el acarreo.

**6. Transmitir, y medirlo en vez de creérselo.** Cada emisión se captura con el
HackRF y se verifica **bit a bit** contra lo que se esperaba. Las reglas que
hicieron fiable el resultado:

- El HackRF **solo recibe**. Nunca se transmite con él.
- **Control negativo obligatorio** en cada medida: la misma captura con el chip
  en reposo tiene que dar 0 ráfagas.
- **Nunca validar contra una captura preexistente**: cada iteración crea la
  suya y el verificador comprueba la fecha del fichero.
- Una sola variable por iteración.
- El aprobado lo decide una herramienta, no la prosa de quien mide.

**7. El resultado negativo que cambió el diseño.** La idea era *generar* la
trama a partir de los campos entendidos, en vez de llevar capturas. Se hizo, y
en el banco reproducía las 42 sesiones bit a bit… **excluyendo los 73 bits sin
explicar**. Al probarlo contra el ventilador de verdad fallaba de dos formas a
la vez: **todos los botones hacían lo mismo** y **cada uno funcionaba una sola
vez**.

El firmware no tenía ningún bug: emitía la familia y el parámetro correctos de
cada botón, y el contador rotaba. Lo que pasaba es que **lo único que el
generador no variaba era justo lo que el receptor mira**: la subtrama `a04` iba
congelada y los 73 bits sin explicar de `b6d` también. Una sola causa explica
los dos síntomas: el receptor descarta la `b6d` y se queda con una `a04` que
además es siempre idéntica, así que la deduplica.

Conclusión: **esos 73 bits se validan de alguna forma**, y no se pueden inventar.
Por eso el componente **emite las pulsaciones reales capturadas** (`use_bank:
true`), rotando entre las 3 de cada botón, que es lo que vence la deduplicación
del receptor. Son 3.4 KB de flash. La lección metodológica: un test que excluye
el 20% de la trama no autoriza a concluir nada sobre la trama entera.

**8. Coexistencia con el WiFi.** El SX1280 emite en 2402 MHz y el WiFi del
ESP32-S3 está en la misma banda y en la misma placa, así que había que medirlo.
Con el WiFi asociado y cursando tráfico, **en un canal que contiene a 2402 MHz**
(el canal 1 ocupa 2401-2423), la emisión sale **bit-exacta al 100%** (250 de 250
ráfagas). Lo que sí aparece es **jitter de temporización**, y comparando contra
una compilación sin red quedó atribuido: con WiFi, 26 de 246 periodos fuera de
±3%; **sin red, 0 de 170**. Subir la prioridad de la tarea de transmisión lo baja
al 0.8%. El ventilador obedece igual, porque cada paquete es individualmente
correcto.

---

## Limitaciones

- **El estado es optimista.** No hay realimentación de ninguna clase. Si alguien
  usa el mando físico, Home Assistant no se entera.
- **Los comandos son capturas de un mando concreto.** Otra unidad
  probablemente necesite recapturar su propio banco.
- **No hay brillo ni temperatura de color continuos**, solo los tres presets que
  el mando sabe mandar, por lo explicado en el punto 7.
- `cold_warm` y `light_toggle` son **toggles ciegos**: alternan, no fijan.

## Créditos y licencia

La ingeniería inversa, el firmware y el componente son trabajo propio sobre
hardware comprado. Este repo no contiene firmware, claves ni material del
fabricante: solo las tramas capturadas del aire de un mando propio, que es lo
que permite reproducir sus pulsaciones.

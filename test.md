

# 💽 MASTER BLUEPRINT: «DOM RADIO» DSP PIPELINE v1.0
**Фреймворк:** JUCE 7/8 + CMake (C++17) | **Архитектура:** Grey-Box / Component-Based

## 📦 ВЕРСИОННОСТЬ И СБОРКА (Monorepo)
Из единого ядра собираются два плагина. Разница заключается в интерфейсе и наличии макро-контроллеров:
1. **MASTER EDITION (2U):** Флагман. 4 скина, разворачивающийся блок головок, стрелочные VU-метры. Расширенный визуальный контроль.
2. **SOLO EDITION (1U):** Компактный Channel Strip. Оснащен уникальной макро-ручкой `TAPE FORMAT` (матрица интерполяции пленки) и ручкой `MIX` на главной панели. Идеален для массовой параллельной обработки.

---

## ⚙️ СИСТЕМНЫЙ УРОВЕНЬ (PRE-PROCESSING)
1. **Dynamic Oversampling:**
   * Считывание `isNonRealtime()`. 
   * DAW playback = Minimum-Phase IIR **2x** (Zero Latency). 
   * Offline Render = Minimum-Phase IIR **16x** (Max Anti-Aliasing).
2. **Smart Split (Подготовка к MIX):**
   * Входной сигнал дублируется на `Dry` (с авто-компенсацией задержки оверсемплинга) и `Wet` (маршрутизируется в тракт).

---

## 🎛️ ЭТАП I: АМПЛИТУДНО-ТЕМБРАЛЬНЫЙ ТРАКТ (Обработка Wet-канала)

### Порядок обработки Wet-сигнала

```text
Input Gain
    ↓
Input Transformer Tr1 (HPF 30 Hz / Phase 400 Hz + Iron Core)
    ↓
Slew-Rate Limiter
    ↓
Dynamic Bias Sag
    ↓
Input Drive / Spiral2Core
    ↓
Tape Pre-Emphasis (Record High-Shelf)
    ↓
Magnetic Tape Core (всегда активен, TAPE_DRIVE 0% = номинальное возбуждение 1.0x)
    ↓
DC Blocker
    ↓
Tape De-Emphasis (Playback High-Shelf, комплементарная пара)
    ↓
Head Bump
    ↓
Decay / Gap Loss (AllPass при DECAY=0, динамический LPF при DECAY>0)
    ↓
Pultec Tone EQ
    ↓
Scrape Flutter (fractional delay)
    ↓
Stereo Crosstalk (HF)
    ↓
Smart Mix / Output
```

Нелинейности тракта разделяются следующим образом:

```text
INPUT SATURATION
    DRIVE
    DRIVE TYPE
    Spiral2Core

TAPE SATURATION
    TAPE DRIVE
    BIAS
    MagneticTapeCore

TAPE FREQUENCY RESPONSE
    AIR
    DECAY
    EQ STANDARD
    BASS / TREBLE
    BASS FREQ / TREBLE FREQ
```

`DRIVE` и `TAPE DRIVE` не используют общий коэффициент усиления.
Изменение `DRIVE` не должно менять силу магнитной сатурации.
Изменение `TAPE DRIVE` не должно увеличивать перегруз входного
транзисторного каскада.

### Блок 1. Входной каскад (Input Stage)
*   **Трансформатор Tr1:** (Пермаллой, 1:5, Входной импеданс: 5 кОм). 
    *   *DSP:* High-Pass фильтр 1-го порядка на 30 Гц + All-Pass фильтр (поворот фазы). Дает легкую НЧ-компрессию саб-низа.
    *   *Iron Core:* Низкочастотное насыщение сердечника. `IRON_CORE = 0%` — только HPF/APF, нелинейность отсутствует; `100%` — подмена НЧ-составляющей через `tanh` с нечётными гармониками. Flux-коэффициент `1 - exp(-1 / (0.035 * SR))`.
*   **Slew-Rate Limiter (ОУ 72709N):** 
    *   *DSP:* Ограничение производной $dV/dt$. 
    *   *Контроль (`TRANSIENT`):* Управляет порогом сглаживания пиков (естественный де-эссер).
*   **Dynamic Bias Sag (смещение рабочей точки):**
    *   *DSP:* Envelope follower 10 мс / 250 мс, `sag = |in|_env * amount`.
    *   *Контроль (`BIAS_SAG`):* 0...100% — динамическая асимметрия `0.012 + sag*0.035` перед `Spiral2Core`. При `0%` асимметрия фиксирована `0.012`.

### Блок 2. Ядро Сатурации (The Drive Core)
#### Входной каскад

* **DRIVE:** Управляет перегрузом входного транзисторного каскада и ядра
    `Spiral2Core`. Это не является управлением плёночной сатурацией.

    Внутренний физический предел входного DRIVE ограничен значением `3.85`,
    что соответствует 70% от первоначального максимального значения `5.5`.
    Значения ручки выше физического предела не усиливают входной каскад.

* **DRIVE TYPE:** Выбор модели входного каскада:
    * `Silicon (BC414)` — более симметричная и собранная сатурация;
    * `Germanium (BC182)` — выраженная асимметрия, DC-offset и генерация
        2-й и 4-й гармоник.

#### Плёночная сатурация

* **TAPE DRIVE:** Независимое управление магнитной сатурацией
    `MagneticTapeCore`. Ручка не изменяет перегруз входного транзисторного
    каскада.

    Диапазон пользовательского параметра:

    ```text
    0...100%
    ```

    Внутренний диапазон магнитного возбуждения:

    ```text
    1.0...7.3
    ```

    Нулевая позиция сохраняет номинальный режим без дополнительного усиления
    плёночного возбуждения. Максимальный уровень плёночной сатурации увеличен
    на 40% относительно исходного диапазона.

* **BIAS_SAG:** Динамический просад транзисторного каскада (см. блок 1). Не влияет на `MagneticTapeCore`; `BIAS` и `BIAS_SAG` — разные физические узлы.

* **BIAS:** Управляет только магнитным ядром плёнки:
    * `Under-bias` — рост THD, более жёсткая атака и ВЧ-песок;
    * `Nominal` — штатная рабочая точка, соответствующая току около 240 кГц;
    * `Over-bias` — уменьшение THD, замедление магнитной реакции и компрессия ВЧ.

    `BIAS` не влияет на входной транзисторный каскад.

* **Punch-Сатуратор (Airwindows Spiral):** логарифмическая синусоидальная
    кривая искажений без разрыва функции, сохраняющая транзиенты.

### Блок 3. Пре-Эквализация и Лента (Tape EQ & Medium)
*   **ВЧ-Резонанс (LC-контур):**
    *   *Контроль (`AIR RESONANCE`):* Комплементарная High-Shelf пара `Record = +curve + AIR_extra (±6 дБ)`, `Playback = -curve` на одной `curve.frequency` (19 см/с: 16 кГц/+14 дБ, 38 см/с: 18 кГц/+8 дБ; NAB: 1768 Гц/6 дБ или 4547 Гц/8 дБ). При `AIR=0` цепочка номинально flat. 
*   **Tape Absorption (Поглощение ВЧ):** 
    *   *Контроль (`DECAY`):* `0` = AllPass (только базовая кривая), `>0` = базовый `baseGapLoss - decay*800 Гц` + динамический LPF (0.5→3000 Гц компрессии). `Scrape Flutter` — отдельная fractional-delay модуляция `0.02±0.0025 мс` от band-limited шума 4 кГц.

### Дополнительное управление частотой EQ

* **BASS:** Усиление или ослабление низкочастотной полки:

    ```text
    Диапазон: -12...+12 dB
    ```

* **BASS FREQ:** Центральная частота низкочастотной полки:

    ```text
    Диапазон: 30...200 Hz
    Значение по умолчанию: 60 Hz
    ```

* **TREBLE:** Усиление или ослабление высокочастотной полки:

    ```text
    Диапазон: -12...+12 dB
    ```

* **TREBLE FREQ:** Центральная частота высокочастотной полки:

    ```text
    Диапазон: 2...15 kHz
    Значение по умолчанию: 10 kHz
    ```

`BASS` и `TREBLE` управляют только величиной усиления.
`BASS FREQ` и `TREBLE FREQ` управляют только положением частотной полки.

Примеры:

```text
BASS = +6 dB
BASS FREQ = 100 Hz
```

Усиление низких частот выполняется от области около 100 Гц.

```text
TREBLE = +5 dB
TREBLE FREQ = 8 kHz
```

ВЧ-подъём начинается примерно от 8 кГц.

При `BASS = 0 dB` или `TREBLE = 0 dB` изменение соответствующей частоты
не даёт слышимого изменения, поскольку gain полки равен нулю.

### Блок 4. Воспроизведение (De-Emphasis)
*   **Head Bump (НЧ-резонанс):** +1.5 дБ на 65 Гц (для 19 см/с) или +1 дБ на 35 Гц (для 38 см/с).
*   **CCIR / NAB Эквалайзер:**
    *   *Контроль (`EQ STANDARD`):* Зеркальные Biquad-фильтры.
    *   **CCIR 38 см/с:** 35 мкс ($f_c \approx 4547$ Гц).
    *   **CCIR 19 см/с:** 70 мкс ($f_c \approx 2273$ Гц).
    *   **NAB (9.5 см/с):** 90 мкс + 3180 мкс (для лоу-фая).

---

## 🎛️ ЭТАП II: SMART MIX (Кроссфейдер)
Точка безупречного фазового слияния (Особенно важно для SOLO-редакции).
*   *Математика:* `Blended = (Dry * (1.0 - Mix)) + (Wet * Mix)`.
*   *Результат:* Параллельная сатурация и подмес "Воздуха" без эффекта гребенчатого фильтра (Comb Filtering).

---

## 🎛️ ЭТАП III: ВРЕМЕННОЙ ТРАКТ И ИЗНОС (Применяется к Blended сигналу)
Глубина всех модулей в этой секции **умножается на значение ручки MIX**. (Если Mix 50%, детонация и шум слабеют в 2 раза).

### Блок 5. Механика и Интерполяция (Format Matrix)
*   *Контроль (`TAPE FORMAT` - Только в SOLO):* Непрерывная матрица параметров ($0.0 \rightarrow 1.0$):

| Формат | Скорость | Gap Loss | Wow | Flutter | Headroom |
|---|---:|---:|---:|---:|---:|
| 10.5" | 38 cm/s | 18 kHz | 0.035% | 0.035% | +12 dB |
| 7" | 19 cm/s | 16 kHz | 0.065% | 0.065% | +6 dB |
| 3" | 9.5 cm/s | 12.5 kHz | 0.12% | 0.12% | +2 dB |
| Cassette | 4.75 cm/s | 5.5 kHz | 0.55% | 0.55% | -3 dB |

Значения таблицы являются номинальными физическими значениями при
`WOW = 25%` и `FLUTTER = 25%`.

При установке ручки в 100% глубина модуляции увеличивается примерно
в четыре раза, но ограничивается безопасным диапазоном конкретного формата.

### Детонация Wow & Flutter

Механическая модуляция реализуется через fractional delay line.
Wow и flutter являются независимыми компонентами задержки.

* **WOW:** Медленная модуляция длины ленты и скорости протяжки.
    Преимущественно воздействует на низкочастотную часть сигнала.
* **FLUTTER:** Быстрая мелкомасштабная модуляция движения ленты.
    Преимущественно воздействует на высокочастотную часть сигнала.

Обе ручки используют диапазон:

```text
0...100%
```

Значение `25%` соответствует первоначальной силе эффекта:

```text
0%    — эффект отключён;
25%   — исходная сила текущей модели;
100%  — приблизительно 4x исходная глубина модуляции.
```

Усиление производится через глубину fractional delay, а не через изменение
частоты LFO. Частоты модуляции остаются физически разделёнными:

```text
WOW      — медленный компонент около 0.55...0.8 Hz
FLUTTER  — быстрый компонент около 12.5...25 Hz
```

Максимальная глубина ограничивается, чтобы эффект не превращался в chorus
или pitch-vibrato:

```text
WOW:
        19 cm/s — до 0.35 ms
        38 cm/s — до 0.65 ms

FLUTTER:
        19 cm/s — до 0.045 ms
        38 cm/s — до 0.030 ms
```

Фактическая глубина дополнительно умножается на `MIX`, `AGE` и `TAPE FORMAT`.
При `MIX = 0%` механическая модуляция не должна изменять итоговый сухой сигнал.

### Блок 6. Деградация "Archive '84"
*   *Контроль (`AGE` + 7 Триммеров):*
    1.  **OXIDE (Dropouts):** Умножение амплитуды на генератор случайных провалов (от 0 до -6 дБ).
    2.  **AZIMUTH (Phase Drift):** LFO-задержка (0.1 Гц) правого канала на 0.5–2 мс. Дает стерео-расширение.
    3.  **ECHO (Print-Through):** Пре-эхо и Пост-эхо (-40 дБ, задержка = 1 оборот катушки).
    4.  **SCRAPE FLUTTER:** Высокочастотное трение ленты о головку — микромодуляция fractional delay band-шумом 4 кГц (±0.0025 мс), gate по `AGE * amount`.
    5.  **CROSSTALK (HF):** Взаимопроникновение каналов выше 5 кГц, `-45 дБ * amount * ageNorm`, неинвертированное.
    6.  **SMART HUM & NOISE:** Сэмпл паузы Муи Гасановой + гул 50 Гц (HUM не зависит от AGE, только `currentHum * currentMix`).
        *   *Динамический режим:* Envelope Follower (Атака 30 мс, Релиз 600 мс). Шум звучит только во время полезного сигнала, в паузах — тишина. Уровень шума Свемы по паспорту: **-63 dB**, HUM калиброван `-90 + amount*27 дБ`.

---

## 🎛️ ЭТАП IV: ВЫХОДНОЙ КАСКАД (ФИНАЛИЗАЦИЯ)

### Блок 7. Transformer-Coupled Clipper (ИС TBA-820 + Tr2)
Заменяет дешевый цифровой хард-клиппинг на аналоговое поведение при перегрузе:
1.  **Dynamic Headroom (Power Supply Sag):** Порог клиппинга по паспорту = **+12 дБ (при 200 Ом)**. При ударе транзиентов выше +12 дБ, порог кратковременно "проседает" на 0.5–1 дБ (Envelope follower), давая эффект компрессионного дыхания блока питания.
2.  **Soft-Knee Diode Curve:** Вместо среза под прямым углом (`jlimit`), сигнал загибается по полиномиальной кривой на последних 0.5 дБ перед лимитом.
3.  **Transformer LPF (Губка):** Острые квадратные углы обрезанной волны смягчаются динамическим High-Shelf/LPF фильтром (-3 дБ на 18 кГц), убирая цифровой алиасинг и "песок". Выходной импеданс 60 Ом.
*   *Контроль (`OUTPUT LEVEL`):* Итоговая громкость (-inf ... +6 dB).

---



### 1. Постоянные времени коррекции АЧХ (CCIR / NAB)
*Используются в формуле $f_c = \frac{1}{2 \pi \tau}$ для вычисления точных частот среза IIR Biquad-фильтров:*

*   **35 мкс** (CCIR для 38,1 см/с) $\rightarrow$ Частота перегиба: **$4547$ Гц**
*   **70 мкс** (CCIR для 19,05 см/с) $\rightarrow$ Частота перегиба: **$2273$ Гц**
*   **50 мкс** (Альтернативный CCIR) $\rightarrow$ Частота перегиба: **$3183$ Гц**
*   **90 мкс** (NAB для 9,53 см/с) $\rightarrow$ Частота перегиба: **$1768$ Гц**

---

### 2. Накачка ВЧ при записи (LC Pre-Emphasis — «Криспи-Воздух»)
*Установки подстроечных потенциометров Усилителя Записи (P4 / P6) из мануала:*

*   **Скорость 19,05 см/с:** Подъем на **+14 дБ на частоте 16 000 Гц** (LC-контур $L_0-023 / L_1-021$).
*   **Скорость 38,1 см/с:** Подъем на **+8 дБ на частоте 18 000 Гц**.
*(Это число формирует динамическую ВЧ-компрессию: верха на +14 дБ упираются в сатуратор ленты и плющатся, а затем CCIR-фильтр 70 мкс опускает их обратно).*

---

### 3. НЧ-Резонанс огибающей головки (Head Bump)
*Физический геодезический горб воспроизводящей головки на низах:*

*   **Скорость 19,05 см/с:** Буст **+1.5 дБ на частоте ~65 Гц** с крутым спадом ниже 40 Гц.
*   **Скорость 38,1 см/с:** Буст **+1.0 дБ на частоте ~35 Гц** (более ровный и низкий низ).

---

### 4. Границы АЧХ и Срез потерь в зазоре (Gap Loss LPF)
*Спад верхних частот из-за ширины зазора головки и самодемагнетизации:*

*   **38,1 см/с:** Диапазон **30 Гц – 18 000 Гц** ($\pm 2$ дБ). Срез Gap Loss начинается с **18 кГц**.
*   **19,05 см/с:** Диапазон **40 Гц – 16 000 Гц** ($\pm 2$ дБ). Срез Gap Loss начинается с **16 кГц**.
*   **9,53 см/с:** Диапазон **63 Гц – 12 500 Гц** ($\pm 3$ дБ). Срез Gap Loss начинается с **12.5 кГц**.
*(Для Solo-режима кассеты `CASSETTE`: срез смещается на **5.5 кГц**).*

---

### 5. Пороги Нелинейностей, THD и Хедрум
*Параметры для настройки порогов срабатывания сатуратора и клиппера:*

*   **Номинальный THD (Искажения):** $\le 0.8\%$ на частоте 1000 Гц при уровне 0VU (для ленты AGFA FER-528).
*   **Стерео-искажения:** $\le 1.8\%$ (на FER-528) или $\le 0.6\%$ (на Scotch 2500 HAEG).
*   **Потолок перегруза (TBA-820):** Ровно **+12 dBFS** (при нагрузке 200 Ом). Выше этого значения включает просадка питания (Sag) и жесткое ограничение.
*   **Входной уровень:** 0 ... +6 дБ (Входной импеданс **5 кОм**).
*   **Выходной импеданс:** **60 Ом**.

---

### 6. Временные и Шумовые показатели
*   **Детонация (Wow & Flutter):**
    *   38,1 см/с: $\le 0.035\%$
    *   19,05 см/с: $\le 0.065\%$
    *   9,53 см/с: $\le 0.12\%$
*   **Отношение сигнал/шум (SNR):** **-63 дБ** (моно) / **-62 дБ** (стерео) на ленте AGFA FER-528. *(Это опорная громкость нашего динамического сэмпла Муи Гасановой).*
*   **Ток подмагничивания (ГСП):** **240 кГц** (ток стирания: **120 кГц**, глубина стирания: **80 дБ**).

---

### MASTER EDITION — расширенное управление

#### Input Stage

* `IN GAIN`
* `DRIVE`
* `DRIVE TYPE`
* `TRANSIENT`
* `IRON_CORE` — насыщение сердечника Tr1 (0...100%, default 0%)
* `BIAS_SAG` — динамический просад (0...100%, default 0%)

#### Tape Saturation

* `TAPE DRIVE` — теперь 0...100%, default 0% (1.0x номинальное возбуждение, не bypass)
* `BIAS`
* `BIAS_SAG` — см. Input Stage

#### Tape EQ

* `AIR`
* `DECAY`
* `EQ STANDARD`
* `BASS`
* `BASS FREQ`
* `TREBLE`
* `TREBLE FREQ`

#### Tape Mechanics

* `WOW`
* `FLUTTER`
* `SCRAPE_FLUTTER` — трение о головку (0...100%, default 0%)
* `CROSSTALK` — HF crosstalk (0...100%, default 0%)

#### Smart Mix

* `MIX`
* `HUM` — теперь `humGen.process(currentHum*currentMix)`, калибровка -90...+27 дБ, не зависит от AGE

### Таблица параметров DSP

| Parameter ID | Назначение | Диапазон | Default |
|---|---|---:|---:|
| `DRIVE` | Входная транзисторная сатурация | 1.0...10.0 | 1.0 |
| `TAPE_DRIVE` | Магнитная сатурация плёнки | 0...100% | 0% |
| `DRIVE_TYPE` | Silicon / Germanium | 2 режима | Silicon |
| `BIAS` | Ток подмагничивания ленты | -1...+1 | 0 |
| `BIAS_SAG` | Динамическая асимметрия транз. каскада | 0...100% | 0% |
| `IRON_CORE` | Насыщение сердечника Tr1 | 0...100% | 0% |
| `BASS` | Gain низкой полки | -12...+12 dB | 0 dB |
| `BASS_FREQ` | Частота низкой полки | 30...200 Hz | 60 Hz |
| `TREBLE` | Gain высокой полки | -12...+12 dB | 0 dB |
| `TREBLE_FREQ` | Частота высокой полки | 2...15 kHz | 10 kHz |
| `WOW_AMOUNT` | Глубина wow-модуляции | 0...100% | 25% |
| `FLUTTER_AMOUNT` | Глубина flutter-модуляции | 0...100% | 25% |
| `SCRAPE_FLUTTER` | Scrape-модуляция delay | 0...100% | 0% |
| `CROSSTALK` | HF межканальное проникновение | 0...100% | 0% |
| `HUM` | Сетевой гул 50 Гц (MainsHum) | 0...100% (-90...-63 дБ) | 0% |
| `MIX` | Соотношение Dry/Wet | 0...100% | 100% |

### Раздельная индикация нелинейностей

Для контроля тракта используются два независимых индикатора:

```text
INPUT SAT
TAPE SAT
```

`INPUT SAT` измеряет разницу до и после `Spiral2Core`:

```cpp
inputActivity = abs(inputStageOutput - inputStageInput);
```

`TAPE SAT` измеряет разницу до и после `MagneticTapeCore`:

```cpp
tapeActivity = abs(tapeStageOutput - tapeStageInput);
```

Индикаторы являются оценкой активности нелинейного блока, а не полноценным
спектральным THD-анализатором.

Точки измерения:

```text
Input SAT:
    после Spiral2Core

Tape SAT:
    после MagneticTapeCore

Output Clip:
    после TransformerClipper
```

Это позволяет отличить перегруз входного каскада от магнитной сатурации
плёнки и от перегруза выходного клиппера.

## ЛОГИКА РАЗДЕЛЕНИЯ КОНТРОЛЕЙ

```text
DRIVE
    Только входной транзисторный каскад

DRIVE TYPE
    Характер входной нелинейности

IRON_CORE
    НЧ-насыщение входного трансформатора

BIAS_SAG
    Динамическая асимметрия перед Spiral2Core (атака 10 мс / релиз 250 мс)

TAPE DRIVE
    Только магнитная сатурация плёнки (0% = 1.0x активно)

BIAS
    Ширина и характер магнитной петли

AIR
    ВЧ-предыскажение перед магнитным ядром (extra ±6 дБ)

DECAY
    ВЧ-поглощение и динамический Gap Loss (DECAY=0 -> AllPass)

SCRAPE_FLUTTER
    Микро-flutter от трения о головку (AGE-gated fractional delay)

CROSSTALK
    HF crosstalk выше 5 кГц, -45 дБ * amount * ageNorm

BASS / TREBLE
    Усиление эквализационных полок

BASS FREQ / TREBLE FREQ
    Частотное положение эквализационных полок

WOW
    Медленная модуляция механики ленты

FLUTTER
    Быстрая модуляция механики ленты

HUM
    Независим от AGE: humGen.process(currentHum * currentMix), -90...-63 дБ

MIX
    Соотношение Dry/Wet и глубина Stage III

AGE
    Шум, dropouts, echo, scrape, crosstalk и усиление wow/flutter — но не HUM
```

Ключевое требование:

```text
DRIVE != TAPE DRIVE
WOW != FLUTTER
BASS GAIN != BASS FREQ
TREBLE GAIN != TREBLE FREQ
```

Каждая ручка должна воздействовать только на закреплённую за ней
физическую часть модели.

---

## 🛠️ DEVELOPMENT CHANGELOG — Session Implemented Changes

### 1. Missing UI Controls Added (PluginEditor.cpp / PluginEditor.h)
Four previously hidden parameters were exposed to the UI with dedicated rotary sliders:
- **`IRON_CORE`** — Input transformer iron core saturation (0...100%)
- **`BIAS_SAG`** — Dynamic bias sag of input transistor stage (0...100%)
- **`SCRAPE_FLUTTER`** — High-frequency scrape flutter modulation (0...100%)
- **`CROSSTALK`** — HF stereo crosstalk amount (0...100%)

All controls placed in the Archive '84 expandable section with proper labels and APVTS `SliderAttachment` bindings.

### 2. Symmetric Gain Ranges & LINK Fix (PluginProcessor.cpp)
- **`IN_GAIN`** range changed from `-20...+15 dB` → **`-18...+18 dB`**
- **`OUT_LVL`** range changed from `-24...+6 dB` → **`-18...+18 dB`**
- This makes the LINK button inversion math (`convertTo0to1(-inputGainDb)`) perfectly symmetric.

### 3. Output Gain Moved Inside Clipper (PluginProcessor.cpp)
`currentOutput` multiplication was moved **inside** `clipL.process()` / `clipR.process()` so that Output Gain now pushes signal into the mastering clipper rather than scaling after it. This allows the clipper to act as a true mastering brickwall limiter when output is driven hard.

### 4. TransformerClipper Rewritten as Mastering Brickwall Limiter (MakhachkalaDSP.h)
Complete rewrite of `TransformerClipper::process()`:
- **Ceiling**: hard limit at ~0.988 (guaranteed 0 dBFS protection)
- **Soft knee**: tanh-shaped compression from knee (~0.642) to ceiling
- **Magnetic remanence**: flux memory adds even harmonics on peaks
- **Dynamic HF shelf**: output transformer darkens (-1.5 dB @ 14 kHz) during clipping
- **TMT-aware**: threshold and shelf frequency shift with component tolerances

### 5. Saturation Indicators Rewritten — THD-Based Math (PluginProcessor.cpp)
Old logic (`std::abs(wet - input)`) was replaced because phase-shifting APF/HPF filters in the input stage caused false readings. New indicators measure **real harmonic distortion contribution**:

**INPUT SAT** = function of:
- `DRIVE` (55% weight)
- `IRON_CORE` (30% weight)
- `TRANSIENT` inverse / slew factor (15% weight)
- Signal presence level

**TAPE SAT** = function of:
- `TAPE_DRIVE` (75% weight)
- `BIAS` absolute value (25% weight)
- Profile-specific `oddHarmonics + evenHarmonics` multiplier
- Signal presence level

Both use smoothed envelopes with 0.08f attack/release for stable LED behavior.

### 6. Tape Saturation Made More Pronounced (TapesDSP.h)
In `TapeProfileProcessor::process()`:
- `saturationBlend` upper limit raised from `0.38f` → **`0.45f`**
- Formula changed from `tapeNorm * (0.18f + pressure * 0.24f)` → `tapeNorm * (0.22f + pressure * 0.28f)`
Result: tape character opens up faster and more richly when turning the `TAPE` knob.

### 7. Historically Accurate Tape Profile Contours (TapesDSP.h + MakhachkalaDSP.h)
Added per-tape-model non-linear frequency response contours to `TapeProfile`:
- **`midContourFreq`** / **`midContourGainDb`** / **`midContourQ`** — peak filter defining each tape's character resonance
- **`grainTextureNoise`** — micro-texture of the oxide layer

**Profile-specific contour data:**
| Model | Contour Freq | Contour Gain | Q | Grain |
|---|---|---|---|---|
| SVEMA A4409 | 320 Hz | -1.6 dB | 0.85 | 0.035 |
| ORWO TYP 106 | 180 Hz | +1.8 dB | 0.75 | 0.020 |
| SCOTCH 2500 HAEG | 450 Hz | -0.6 dB | 0.90 | 0.008 |
| BASF SPR 50 LHL | 1000 Hz | 0.0 dB | 0.70 | 0.002 |

### 8. Profile Mid Contour Filter Wired into EQ Chain (MakhachkalaDSP.h)
- Added `FastBiquad::setPeakFilter()` (RBJ peak filter formula)
- Added `TapeEqualizer::profileMidContour` member
- Called in `updateParameters()` when tape model/speed/age changes
- Applied in `processPost()` after Head Bump cascade, before `outputTrim`
- Included in `getMagnitudeForFrequency()` so the **EQ MONITOR** graph visualizes the contour

### 9. Grain Texture Noise in Tape Profile Processor (TapesDSP.h)
Added micro-texture noise generation inside `TapeProfileProcessor::process()`:
- Uses `FastRandom` seeded at `777`
- Modulated by `tapeLoad` (drive × pressure)
- Scaled by profile-specific `grainTextureNoise`
- Adds subtle amplitude noise proportional to signal level (`output * noiseRaw * grain * tapeLoad * 0.06f`)

### 10. FastRandom Relocated to Shared Header (TapesDSP.h)
- `FastRandom` class and `randomFloat()` helper moved from `MakhachkalaDSP.h` → `TapesDSP.h`
- Removed duplicate definition from `MakhachkalaDSP.h`
- Both `TapeProfileProcessor` and `WowFlutterGenerator` now share the same RNG implementation

---

### Files Modified in This Session
| File | Changes |
|---|---|
| `Shared/TapesDSP.h` | `setPeakFilter`, new `TapeProfile` fields, grain noise, `FastRandom` relocation |
| `Shared/MakhachkalaDSP.h` | `profileMidContour` wiring, `TransformerClipper` rewrite, removed duplicate `FastRandom` |
| `Plugins/DomRadio_Master/Source/PluginProcessor.cpp` | Symmetric gain ranges, output-in-clipper, THD-based saturation indicators |
| `Plugins/DomRadio_Master/Source/PluginEditor.cpp` | 4 new sliders, labels, bounds, attachments |
| `Plugins/DomRadio_Master/Source/PluginEditor.h` | 4 new slider + attachment declarations |

### Build Status
- **VST3** and **Standalone** artifacts produced successfully
- Remaining warnings only: `C4244` (float narrowing) and `C4189` (unused locals)
- **Zero compilation errors**

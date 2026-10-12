# Elephant Calc 🐘

A lightning-fast, keyboard-driven Spotlight calculator for Linux desktops. Written in pure C (C11), rendered directly via **Xlib**, **Cairo**, and **FreeType** with zero web bloat, instant startup (< 50ms), and a rich multi-row HUD.

---

## 🌟 Key Features

- **Spotlight Floating HUD**: Centered floating bar designed for rapid keyboard workflows.
- **Sub-50ms Cold Launch**: Pure C11 native binary with zero Electron, Node, Python, or GTK runtime overhead.
- **Context-Aware Smart Autocomplete**:
  - Dynamically scores and ranks suggestions based on preceding tokens and unit dimensions.
  - Automatically suggests compatible units (e.g., `50 km in m` $\rightarrow$ `meters` prioritized over `min()`).
  - Contextual timezone suggestions (e.g., `now in t` $\rightarrow$ `tokyo` prioritized over `tan()`).
  - Calendar operator suggestions (e.g., `5 days b` $\rightarrow$ `before` prioritized over `berlin`).
  - Function promotion after math operators (e.g., `10 + s` $\rightarrow$ `sqrt()` prioritized over `seconds`).
  - Number scale promotion after numeric values (e.g., `25 cr` $\rightarrow$ `crore` prioritized over `cbrt()`).
  - Multi-candidate preview bar with interactive `Tab` cycling (`Tab: [candidate1] • candidate2 (1/N)`).
  - Dynamic auto-discovery for user-defined variables and custom functions.
- **Universal Compound Unit Systems**:
  - Time domain: `(5h+2min) in hr` $\rightarrow$ `5 hr 2 min`.
  - Imperial length: `5.75 ft in ft` $\rightarrow$ `5 ft 9 in`, `70 in` $\rightarrow$ `5 ft 10 in`.
  - Adjacent compound parsing: `5ft 9in + 3in` $\rightarrow$ `6 ft`.
  - Imperial mass: `8.5 lb in lb` $\rightarrow$ `8 lb 8 oz`, `100 oz` $\rightarrow$ `6 lb 4 oz`.
  - Multi-unit breakdown subtitles across HUD and CLI.
- **Persistent Formulas & User-Defined Functions**:
  - Ephemeral session assignment via `=` (`rate = 18%`).
  - Persistent disk-backed assignment via `:=` (`tax := 18%`, saved across restarts in `~/.config/elephant-calc/saved_symbols.txt`).
  - Multi-parameter custom functions (`f(x) = x^2 + 1`, `hypot(a, b) := sqrt(a^2 + b^2)`).
  - Continuous evaluation: changing variables automatically updates dependent formulas.
  - In-app symbol management: `vars` / `saved` to list symbols; `delete <name>` / `undef <name>` to remove.
- **Intelligent Bracket Engine**:
  - Operand-aware `(` auto-wrapping: `10 + 250` $\rightarrow$ `10 + (|250)`.
  - Hierarchical retroactive cut cycling on repeated `)` taps with two-tier ghost brackets.
  - Non-modal delimiter auto-commit and intelligent backspace/delete on empty pairs `()`.
  - Tolerant EOF evaluation (`sqrt(144` automatically evaluates cleanly to `12`).
- **Date, Time & Calendar Intervals**:
  - Named months, ISO dates (`YYYY-MM-DD`), `today`, `now`, `tomorrow`, `yesterday`.
  - Date interval queries: `between 10-Oct and 15-Oct`, `until`, `since`.
  - Relative offsets: `5 days before today`, `3 weeks after 1-Nov`.
  - Business & working days: `bday`, `bizday`, `workday` (`5 bday after today`).
  - Epoch conversions (`to date`, `to utc`, `to epoch`).
- **Global Timezone Conversions**:
  - Convert time across world cities, standard abbreviations, and numeric offsets (`now in tokyo`, `14:30 in tokyo`, `now in utc+5:30`).
  - Cross-timezone conversion: `5pm est in ist`, `9am tokyo in london`.
  - Dynamic IANA DST tracking with day-boundary shift tags (`Tomorrow`, `Yesterday`).
- **Programmer Mode**: Hex (`0x`), Binary (`0b`), Octal (`0o`), bitwise operators (`&`, `|`, `~`, `<<`, `>>`, `xor`), and radix conversions (`to hex`, `to bin`, `to oct`, `to dec`).
- **Readline / Emacs Keybindings**: Full non-modal CLI navigation (`Ctrl+A`, `Ctrl+E`, `Ctrl+B`, `Ctrl+F`) and line editing (`Ctrl+U`, `Ctrl+K`, `Ctrl+W`, `Ctrl+D`, `Ctrl+H`).
- **Undo / Redo History**: Full non-linear buffer state history via `Ctrl+Z` (Undo) and `Ctrl+Y` (Redo).
- **Financial & Loan Suite**: Built-in functions for `emi()`, `sip()`, `cagr()`, `compound()`, `discount()`, and `markup()`.
- **Dimensional Unit Conversions**: 10 physical dimensions (Length, Mass, Time, Data Storage, Temperature, Currency, Speed, Area, Data Transfer Rates, Pressure) with scale multipliers (`k`, `lakh`, `crore`, `million`, `billion`).
- **Dual CLI & GUI**: Headless pipe evaluation (`--eval "<expr>"`) alongside the X11 GUI.

---

## 🖥️ User Interface Overview

```text
+------------------------------------------------------------------------+
| > sqrt(144) + 2pi                                                      |  <- Row 1: Math Input (Full Width)
+------------------------------------------------------------------------+
|                                                              = 18.2832 |  <- Row 2: Live Evaluated Result (Click to Copy)
+------------------------------------------------------------------------+
| ans = 18.2832  •  tax = 18%  •  total = 1,416          [✕ Delete All] |  <- Row 3: Variables (Persistent ans + User Vars)
+------------------------------------------------------------------------+
| Eighteen point Two Eight Three Two                                     |  <- Row 4: Spoken Words (Drag-to-Copy)
+------------------------------------------------------------------------+
| Base: Hex: 0x12  •  Bin: 0b10010                                       |  <- Row 5: Dedicated Computing Base
+------------------------------------------------------------------------+
| [Enter] Calc   [Tab] Complete   [Ctrl+C] Copy   [↑/↓] History   [⚙]    |  <- Row 6: Shortcuts & Drawer Toggle
+------------------------------------------------------------------------+
```

### Contextual Autocomplete HUD
When typing a prefix, Elephant Calc displays ghost-text inline completion and a candidate preview bar:
```text
Input:  > 50 km in m|
HUD:    Tab: [meters]  •  meter  •  millimeters  •  ... (1/5)
Result: = 50,000 meters
```

### Expandable Settings Tray (`Ctrl + ;` or `⚙`)
```text
+------------------------------------------------------------------------+
| Format:   [Intl (1,000,000)]   [Indian (10,00,000)]   [Plain]   [1/2/3]|
| Decimals: [Auto]  [0]  [2]  [3]  [4]  [6]  [8]            [d] Cycle   |
| Output:   [Comp-Only (Hex/Bin)]   [All (Dec/Hex/Bin)]   [Simple] [c/a/s]|
| Scale:    [80%]  [100%]  [120%]  [140%]  [160%]  [180%] [Ctrl +/- / 0] |
| Rates:    [ ⟳ Refresh ]  ✓ Up to date (1 $ = 96.96 ₹)                  |
+------------------------------------------------------------------------+
```

---

## 🧠 Smart Context-Aware Autocomplete Engine

Unlike traditional calculators that suggest static alphabetical lists, Elephant Calc inspects preceding tokens and mathematical context to rank completions intelligently:

| Context Detected | Preceding Pattern | Prefix | Top Suggestion | Prioritized Ahead Of |
|---|---|---|---|---|
| **Dimension Target** | `50 km in ` | `m` | `meters` | `min()`, `minutes`, `mod()` |
| **Timezone Target** | `now in ` | `t` | `tokyo` | `today`, `tan()` |
| **Currency Target** | `1500 usd in ` | `e` | `eur` | `exp()`, `emi()` |
| **Calendar Operator** | `5 days ` | `b` | `before` | `berlin` |
| **Math Operator** | `10 + ` | `s` | `sqrt()` | `seconds` |
| **Scale Multiplier** | `25 ` | `cr` | `crore` | `cbrt()` |
| **User Symbols** | (any context) | `cust` | `custom_rate` | Built-in symbols |

- **Ghost Text**: The top candidate is displayed inline as faint ghost text touching your cursor.
- **Tabbing & Cycling**: Press <kbd>Tab</kbd> to commit the suggestion. If multiple candidates exist, subsequent <kbd>Tab</kbd> presses cycle through matches in-place (`1/N`).
- **Mid-Buffer Insertion**: Autocomplete works cleanly anywhere within an expression, shifting tail text automatically.

---

## ⚖️ Universal Compound Unit Systems

Elephant Calc natively formats, parses, and converts mixed multi-unit systems across time, length, and mass.

### 1. Time Domain Compounding
```text
> (5h+2min) in hr
5 hr 2 min
(Subtitle: 5.03333333 hours  •  302 minutes  •  18,120 seconds)

> (5h+2min) in min
302 min

> 5400 s in hr
1 hr 30 min
```

### 2. Imperial Length (`ft` & `in`)
Supports decimal compounding and adjacent natural notation:
```text
> 5.75 ft in ft
5 ft 9 in
(Subtitle: 69 in  •  5.75 ft  •  1.7526 m)

> 70 in in ft
5 ft 10 in

> 5ft 9in + 3in
6 ft
```

### 3. Imperial Mass (`lb` & `oz`)
```text
> 8.5 lb in lb
8 lb 8 oz
(Subtitle: 136 oz  •  8.5 lb  •  3.85553515 kg)

> 100 oz in lb
6 lb 4 oz

> 8lb 8oz + 12oz
9 lb 4 oz
```

---

## 📝 Variables, Persistent Formulas & Functions

Elephant Calc distinguishes between session-only variables and persistent disk-backed formulas:

### 1. Ephemeral vs. Persistent Assignment
- **Session Assignment (`=`)**: Resets when Elephant Calc restarts.
  ```text
  > price = 4500
  4,500
  ```
- **Persistent Assignment (`:=`)**: Saved across sessions to `~/.config/elephant-calc/saved_symbols.txt`.
  ```text
  > tax_rate := 18%
  0.18

  > annual_budget := 12 * 85000
  1,020,000
  ```

### 2. User-Defined Functions
Define custom functions with up to 4 parameters:
```text
> f(x) = x^2 + 2x + 1
f(x) = x^2 + 2x + 1

> f(5)
36

> hypot(a, b) := sqrt(a^2 + b^2)
hypot(a, b) := sqrt(a^2 + b^2)

> hypot(3, 4)
5
```

### 3. Continuous Dynamic Evaluation
Formulas retain their symbolic expressions. When an underlying variable changes, dependent values update automatically:
```text
> rate = 10%
> base = 1000
> total = base + base*rate
1,100

> rate = 20%
> total
1,200
```

### 4. In-App Symbol Management
- `vars` or `saved`: Lists all defined variables, functions, and formulas.
- `delete <name>`, `undef <name>`, or `remove <name>`: Deletes a symbol.

---

## 🧠 Intelligent Bracket Engine

Elephant Calc features an intelligent AST-aware bracket predictor:

### 1. Proactive Hierarchical Cut Cycling (Repeated `(`)
When entering `(` before an expression (e.g. at `|10 + 20 * 30`), Elephant Calc scans forward across operator boundaries and cycles candidate closing cut points using two-tier ghost styling:
- **Active candidate**: Rendered in bright cyan (`)`).
- **Alternative candidates**: Rendered in translucent muted blue (`⦆`).
- **Live HUD re-evaluation**: Green result updates live to reflect the active grouping (e.g. `(10 + 20) * 30 = 900` vs `70`).

```text
Input: |10 + 20 * 30

Tap 1: ( 10 + 20 ) ⦆ * 30    -> Wraps compound subexpression (Live HUD = 900)
Tap 2: ( 10 + 20 ⦆ * 30 )    -> Wraps full expression         (Live HUD = 70)
Tap 3: ( 10 ) ⦆ + 20 ⦆ * 30   -> Wraps single operand          (Live HUD = 70)
Tap 4: Cycles back to Tap 1
```

### 2. Retroactive Hierarchical Cut Cycling (Repeated `)`)
When entering `)` with no unclosed opening brackets (e.g. at `3 + 5 * 8|`), Elephant Calc scans backward across operator boundaries and cycles candidate opening positions:
- **Active candidate**: Rendered in bright cyan (`(`).
- **Alternative candidates**: Rendered in translucent muted blue (`⦅`).

```text
Input: 3 + 5 * 8|

Tap 1: ⦅ 3 + ( 5 * 8 )    -> Wraps localized multiplication (Live HUD = 43)
Tap 2: ( 3 + ⦅ 5 * 8 )    -> Wraps full expression         (Live HUD = 43)
Tap 3: Cycles back to Tap 1
```

### 3. Non-Modal Delimiter Auto-Commit
- Pressing any continuation key (`+`, `-`, `*`, `/`, digits, space, or `Enter`) immediately commits the active candidate bracket into real text.
- Pressing `Backspace` or `Escape` cancels the ghost cycle without modifying existing text.

### 4. Smart Backspace & Auto-Close Evaluation
- Backspacing between `(|)` deletes both brackets.
- Missing trailing parentheses automatically auto-close at EOF during evaluation:
  - `./elephant-calc --eval "sqrt(144"` $\rightarrow$ `12`
  - `./elephant-calc --eval "(10 + 20 * (3 + 4"` $\rightarrow$ `150`

---

## 📅 Date, Time & Calendar Calculations

Calculate calendar schedules, countdowns, timestamps, and working days:

| Expression | Example Result | Description |
|---|---|---|
| `between 2026-10-01 and 2026-10-31` | `30.00 days` | Date interval difference |
| `business days between 2026-10-01 and 2026-10-31` | `21 business days` | Working days calculation (excluding weekends) |
| `3 days before 2026-10-25` | `22-Oct-2026` | Relative date offset in past |
| `days until 2026-12-31` | `80.00 days` | Duration countdown to future date |
| `days until tomorrow` | `1.00 day` | Dynamic landmark countdown |
| `days until next month` | `19.00 days` | Dynamic month-boundary countdown |
| `today + 45 days to date` | `26-Nov-2026` | Calendar date 45 days in future |
| `(today + 5 day) to weekday` | `Saturday` | Weekday name |
| `now to utc` | `12-Oct-2026 13:45:00 UTC` | Current timestamp formatted in UTC |
| `10-Sept-2026 to epoch` | `1788978600` | Unix epoch timestamp in seconds |
| `1791590400 to date` | `10-Oct-2026 05:30:00` | Human-readable date with named month |

---

## 🌍 Global Timezone Conversions

Convert current, relative, or specified time across world cities, standard abbreviations, and numeric UTC offsets with automatic Daylight Saving Time (DST) tracking:

| Expression | Example Result | Description |
|---|---|---|
| `now in tokyo` | `12-Oct-2026 10:45:00 PM JST` | Converts current instant to Tokyo time |
| `now in est` | `12-Oct-2026 09:45:00 AM EDT` | Converts current instant to US Eastern Time |
| `5pm in est` | `12-Oct-2026 07:30:00 AM EDT` | Converts 5:00 PM local time to US Eastern Time |
| `14:30 in tokyo` | `12-Oct-2026 06:00:00 PM JST` | Converts 2:30 PM local time to Tokyo time |
| `5pm est in ist` | `Tomorrow, 13-Oct-2026 02:30:00 AM IST` | Cross-timezone conversion with day-boundary shift tag |
| `02:00 in nyc` | `Yesterday, 11-Oct-2026 04:30:00 PM EDT` | Day boundary shift tag for past date |
| `now in utc+5:30` | `12-Oct-2026 07:15:00 PM UTC+05:30` | Numeric UTC offset conversion |
| `now in gmt-8` | `12-Oct-2026 05:45:00 AM UTC-8` | Numeric GMT offset conversion |

- **Words Row**: Displays full geographic region and weekday (e.g., `Tokyo, Japan • Monday`).
- **Computing Row**: Displays relative difference from local time and UTC offset (e.g., `+3h 30m ahead of local • UTC+09:00 • Epoch: 1760288700`).

---

## 💻 Programmer Mode

Full support for radix integer literals, bitwise logic, and base conversions:

| Input Expression | Result | Base Subtitle |
|---|---|---|
| `0xFF + 1` | `256` | `Hex: 0x100  •  Bin: 0b100000000` |
| `0b101010` | `42` | `Hex: 0x2A  •  Bin: 0b101010` |
| `0o77 + 1` | `64` | `Hex: 0x40  •  Bin: 0b1000000` |
| `~0` | `-1` | Bitwise 64-bit NOT |
| `0b1100 & 0b1010` | `8` | Bitwise AND (`0b1000`) |
| `0b1100 \| 0b0011` | `15` | Bitwise OR (`0b1111`) |
| `15 xor 7` | `8` | Infix bitwise XOR (or `xor(15, 7)`) |
| `1 << 10` | `1,024` | Bitwise left shift |
| `1024 >> 2` | `256` | Bitwise right shift |
| `255 to hex` | `0xFF` | Conversion to hexadecimal string |
| `42 to bin` | `0b101010` | Conversion to binary string |
| `100 to oct` | `0o144` | Conversion to octal string |
| `0xFF to dec` | `255` | Conversion to decimal string |

---

## 💰 Financial & Loan Suite

Accurate compound interest, loan amortizations, and returns:

| Function | Formula / Description | Example | Output |
|---|---|---|---|
| `emi(P, r, n)` | Monthly EMI for principal `P`, annual rate `r%`, `n` months | `emi(5000000, 8.5, 240)` | `43,391.16` |
| `sip(P, r, y)` | Systematic Investment Plan future value (`P`/mo, `r%` p.a., `y` yrs) | `sip(10000, 12, 10)` | `23,23,390.76` |
| `cagr(v_i, v_f, y)` | Compound Annual Growth Rate (`v_i` to `v_f` over `y` yrs) | `cagr(100000, 250000, 5)` | `20.11%` |
| `compound(P, r, y)` | Compound interest future value | `compound(100000, 7, 5)` | `1,40,255.17` |
| `discount(price, pct)` | Net amount after percentage discount | `discount(1200, 15)` | `1,020` |
| `markup(cost, margin)` | Selling price with profit margin percentage | `markup(100, 25)` | `125` |

---

## 📊 Statistical & Aggregate Functions

Multi-argument statistical and list functions supporting dynamic scale multipliers:

| Function | Description | Example | Output |
|---|---|---|---|
| `avg(x1, x2, ...)` | Arithmetic mean of values | `avg(100, 200, 300)` | `200` |
| `median(x1, x2, ...)` | Middle value of sorted sequence | `median(5, 1, 9, 3, 7)` | `5` |
| `min(x1, x2, ...)` | Minimum value (supports scales like `k`, `lakh`) | `min(50k, 20k, 90k)` | `20,000` |
| `max(x1, x2, ...)` | Maximum value | `max(15, 85, 42)` | `85` |

---

## 📏 Dimensional Unit Conversions

Supports conversions across 10 dimensions with auto-discovery hints:

- **Length**: `m`, `km`, `cm`, `mm`, `mi`, `yd`, `ft`, `in`
- **Mass**: `g`, `kg`, `mg`, `lb`, `oz`, `ton`
- **Time**: `s`, `min`, `h`, `d`, `week`, `year`
- **Data Storage**: `B`, `KB`, `MB`, `GB`, `TB`, `KiB`, `MiB`, `GiB`, `TiB`
- **Temperature**: `c`, `f`, `k` (affine conversion)
- **Currency**: `USD`, `INR`, `EUR`, `GBP`, `JPY`, `CAD`, `AUD`, `CNY`, `CHF`, `AED`, `SGD`
- **Speed**: `m/s`, `km/h`, `mph`, `knot`, `ft/s`
- **Area**: `sqm`, `sqkm`, `sqft`, `sqin`, `sqyd`, `acre`, `hectare`, `sqmi`
- **Data Transfer Rate**: `bps`, `kbps`, `mbps`, `gbps`, `B/s`, `KB/s`, `MB/s`, `GB/s`
- **Pressure**: `pa`, `kpa`, `bar`, `psi`, `atm`
- **Scale Multipliers**: `k` (1,000), `million` (1e6), `billion` (1e9), `lakh` (100,000), `crore` (10,000,000)

---

## ⌨️ Keyboard & Mouse Controls

### Readline / Emacs Shortcuts
| Shortcut | Action | Description |
|---|---|---|
| <kbd>Ctrl</kbd> + <kbd>A</kbd> | Beginning of line | Moves cursor to start of expression |
| <kbd>Ctrl</kbd> + <kbd>E</kbd> | End of line | Moves cursor to end of expression |
| <kbd>Ctrl</kbd> + <kbd>B</kbd> | Backward character | Moves cursor left 1 character |
| <kbd>Ctrl</kbd> + <kbd>F</kbd> | Forward character | Moves cursor right 1 character |
| <kbd>Alt</kbd> + <kbd>B</kbd> (`M-b`) / <kbd>Ctrl</kbd>+<kbd>←</kbd> | Backward word | Moves cursor backward to start of previous word |
| <kbd>Alt</kbd> + <kbd>F</kbd> (`M-f`) / <kbd>Ctrl</kbd>+<kbd>→</kbd> | Forward word | Moves cursor forward to end of next word |
| <kbd>Ctrl</kbd> + <kbd>U</kbd> | Kill to start | Deletes expression from cursor to beginning |
| <kbd>Ctrl</kbd> + <kbd>K</kbd> | Kill to end | Deletes expression from cursor to end |
| <kbd>Ctrl</kbd> + <kbd>W</kbd> | Kill word backward | Deletes preceding token/word |
| <kbd>Ctrl</kbd> + <kbd>D</kbd> | Forward delete | Deletes character under cursor |
| <kbd>Ctrl</kbd> + <kbd>H</kbd> | Backward delete | Deletes character before cursor |
| <kbd>Ctrl</kbd> + <kbd>Z</kbd> | Undo | Reverts previous buffer modification |
| <kbd>Ctrl</kbd> + <kbd>Y</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> | Redo | Restores previously undone buffer state |

### Navigation, Autocomplete & Settings
| Action | Shortcut / Gesture | Description |
|---|---|---|
| **Commit Autocomplete** | <kbd>Tab</kbd> | Commits active ghost-text suggestion; cycles alternatives on repeat |
| **Commit Expression** | <kbd>Enter</kbd> | Evaluates, saves to history, stores in `ans` memory |
| **Copy Result** | <kbd>Ctrl</kbd> + <kbd>C</kbd> | Copies green evaluated result (or active mouse selection) |
| **History Navigation** | <kbd>↑</kbd> / <kbd>↓</kbd> | Cycles through persistent equation history |
| **Bracket Cycling** | <kbd>)</kbd> or <kbd>(</kbd> | Cycles proactive/retroactive candidate bracket cuts |
| **Settings Drawer** | <kbd>Ctrl</kbd> + <kbd>;</kbd> or <kbd>⚙</kbd> | Opens/closes configuration tray |
| **Number Format** | <kbd>1</kbd> / <kbd>2</kbd> / <kbd>3</kbd> | International (`1,000,000`), Indian (`10,00,000`), or Plain |
| **Decimal Precision** | <kbd>d</kbd> | Cycles decimal places (`Auto` $\rightarrow$ `0` $\rightarrow$ `2` $\rightarrow$ `3` $\rightarrow$ `4` $\rightarrow$ `6` $\rightarrow$ `8`) |
| **Computing Output** | <kbd>c</kbd> / <kbd>a</kbd> / <kbd>s</kbd> | Comp-Only (`Hex • Bin`), All (`Dec • Hex • Bin`), Simple |
| **UI Font Scaling** | <kbd>Ctrl</kbd> + <kbd>+</kbd> / <kbd>-</kbd> / <kbd>0</kbd> | Zooms UI font between 80% and 180% |
| **Dismiss / Cancel** | <kbd>Escape</kbd> | Cancels ghost bracket/autocomplete, closes drawer, or exits |
| **Delete Variables** | Mouse Click | Click `[✕ Delete All]` button on Row 3 |
| **Select Words** | Mouse Drag | Drag across words in Row 4; auto-copies on release |

---

## 💻 Headless CLI Mode

Elephant Calc functions as a standalone command-line calculator for scripts and terminals:

```bash
# Basic evaluation
./elephant-calc --eval "2^32 - 1"

# Intelligent auto-closing parentheses
./elephant-calc --eval "sqrt(144"
# Output: 12

# Compound unit conversion
./elephant-calc --eval "(5h+2min) in hr"
# Output: 5 hr 2 min

# Imperial length compounding
./elephant-calc --eval "5.75 ft in ft"
# Output: 5 ft 9 in

# Date interval calculation
./elephant-calc --eval "between 2026-10-01 and 2026-10-31"
# Output: 30.00 days

# Working days calculation
./elephant-calc --eval "business days between 2026-10-01 and 2026-10-31"
# Output: 21 business days

# Cross-timezone query
./elephant-calc --eval "5pm est in ist"
# Output: Tomorrow, 13-Oct-2026 02:30:00 AM IST

# Customized decimal rounding
./elephant-calc --eval "10 / 3" --decimals 2
# Output: 3.33

# List saved persistent symbols
./elephant-calc --vars

# Delete a saved persistent symbol
./elephant-calc --delete-saved custom_tax

# Plain unformatted number output (no commas)
./elephant-calc --plain --eval "10^6"
# Output: 1000000

# Words & Computing Radix output
./elephant-calc --eval "256" --words
# Output: Hex: 0x100  •  Bin: 0b100000000  •  Two Hundred Fifty Six

# Radix conversions
./elephant-calc --eval "255 to hex"
# Output: 0xFF

# Currency conversion with Indian formatting
./elephant-calc --indian --eval "15 lakh inr to usd"

# Custom config path (isolated environment)
./elephant-calc --config ~/.config/my-calc --eval "rate := 12%"

# Cold startup benchmark
./elephant-calc --benchmark
# Output: Startup to first frame: 41.20 ms
```

---

## 🛠️ Build & Installation

### Requirements (Ubuntu / Debian)
```bash
sudo apt update
sudo apt install build-essential libx11-dev libcairo2-dev libfreetype6-dev
```

### Compile
```bash
git clone https://github.com/your-username/elephant-calc.git
cd elephant-calc
make
```

### Install Globally
```bash
sudo cp elephant-calc /usr/local/bin/
```

### Bind Global Hotkey in GNOME
1. Open **Settings** $\rightarrow$ **Keyboard** $\rightarrow$ **Keyboard Shortcuts** $\rightarrow$ **Custom Shortcuts**.
2. Click **+** (Add Shortcut):
   - **Name**: `Elephant Calc`
   - **Command**: `elephant-calc`
   - **Shortcut**: `Super + Space` (or `Super + C`)
3. Press your hotkey anywhere on the desktop to launch instantly!

---

## 📄 License
MIT License. Built for ultra-fast, native Linux desktop productivity.

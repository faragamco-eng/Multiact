# BLDC-310 — برد درایور و کنترل موتور BLDC (310 V DC، Sensorless)

> **کاربرد هدف: موتور Direct-drive کولر آبی 8000 تا 11000 (outer rotor، 1.2 kW، 450 rpm).** معماری نسخه ۲
> (PFC، FOC، شناسایی خودکار، باس دوسیمه پنل، وای‌فای) در [`docs/04_cooler_drive_architecture.md`](docs/04_cooler_drive_architecture.md) است.

درایور موتور BLDC تک‌فاز ورودی 230 VAC (باس DC حدود 310 V)، توان نامی 750 W، با **STM32G431** و
**IPM 600 V / 15 A**، به روش **Six-step sensorless (BEMF zero-crossing)**.

> ⚠️ **ولتاژ کشنده.** کل مدار، از جمله میکروکنترلر، به برق شهر متصل است. قبل از هر کاری
> [`docs/03_bringup_and_safety.md`](docs/03_bringup_and_safety.md) را بخوانید.

## محتوا

| مسیر | توضیح |
|---|---|
| [`docs/01_hardware_design.md`](docs/01_hardware_design.md) | مشخصات، بلوک دیاگرام، انتخاب قطعات، طبقه قدرت، حفاظت سه‌لایه، اندازه‌گیری، تغذیه، چیدمان |
| [`docs/02_firmware_design.md`](docs/02_firmware_design.md) | الگوریتم sensorless، ماشین حالت، استارت، نتایج شبیه‌سازی، راهنمای تیونینگ |
| [`docs/03_bringup_and_safety.md`](docs/03_bringup_and_safety.md) | راه‌اندازی سه‌مرحله‌ای، چک‌لیست PCB، عیب‌یابی |
| [`docs/04_cooler_drive_architecture.md`](docs/04_cooler_drive_architecture.md) | **کولر آبی:** تحلیل موتورها (36S/40P)، PFC، FOC، شناسایی خودکار، آلارم‌ها، نوسانات برق، باس پنل، وای‌فای |
| [`docs/05_panel_bus_modbus.md`](docs/05_panel_bus_modbus.md) | پروتکل Modbus RTU باس پنل و نقشه رجیسترها |
| [`docs/06_foc_sensorless.md`](docs/06_foc_sensorless.md) | **FOC بدون سنسور + شناسایی خودکار** (R، L، شار، اینرسی، dead-time)، حفاظت قفل روتور، نتایج 110 موتور تصادفی |
| [`docs/07_battery_solar_backup.md`](docs/07_battery_solar_backup.md) | **برق اضطراری:** ماژول باتری ۴۸ V (DAB دوطرفه) + ورودی خورشیدی روی باس DC، زمان کارکرد، نتایج شبیه‌سازی قطع برق |
| [`hardware/v2/`](hardware/v2/README.md) | **شماتیک نسخه ۲** (PFC، FOC سه‌شنت، تغذیه مقاوم 440 VAC، رله پمپ/تخلیه، باس پنل ایزوله، RS-485 ماژول باتری) — بررسی‌شده با کتابخانه KiCad و داده ST، نت‌لیست قابل import در KiCad |
| [`hardware/netlist.md`](hardware/netlist.md) | (نسخه ۱، جایگزین شده) شماتیک متنی برگه‌به‌برگه (پین‌های MCU از دیتابیس رسمی ST) |
| [`hardware/bom.csv`](hardware/bom.csv) | لیست قطعات با پارت‌نامبر نمونه |
| [`hardware/calc/`](hardware/calc/) | اسکریپت محاسبات (تلفات، هیت‌سینک، خازن، بوت‌استرپ، شنت، تقسیم‌کننده‌ها) و نتایج |
| [`firmware/`](firmware/) | فریمور C (هسته کنترل قابل حمل + BSP رجیستری) و شبیه‌ساز مدار بسته |

## شروع سریع

```bash
cd firmware
make cmsis          # یک بار: دریافت هدرهای CMSIS (STM32G4)
make                # build/bldc310.elf/.bin/.hex  (arm-none-eabi-gcc)
make test           # شبیه‌سازی six-step + تست Modbus + آزمون‌های FOC روی PC (gcc)
make test-foc-random  # FOC: 40 موتور تصادفی، شناسایی از صفر و اجرا
make sweep          # مقاوم‌سازی استارت: 5 سناریو × 12 زاویه اولیه روتور
make plots          # نمودارها در docs/img (نیاز به matplotlib)
python3 hardware/calc/bldc_calc.py   # محاسبات طراحی (از ریشه مخزن)
```

فرمان‌های UART (از طریق J2 ایزوله، 115200 8N1): `run`، `stop`، `clr`، `spd <rpm>`، `dir <0|1>`، `pot <0|1>`، `?`

## وضعیت و محدودیت‌ها (صادقانه)

* ✅ فریمور برای STM32G431 بدون هشدار کامپایل می‌شود (≈ 9.8 KB فلش، 3.2 KB RAM).
* ✅ هسته کنترل در شبیه‌ساز سوئیچینگ (IPM + موتور، 0.25 µs) در هر 5 سناریو قبول می‌شود؛ استارت 59/60 در
  جاروب زاویه اولیه؛ 127/162 در جاروب ±30–40 % پارامترهای موتور ⇒ **تیونینگ برای هر موتور لازم است**.
* ⚠️ **هنوز روی سخت‌افزار واقعی تست نشده.** چند کدگذاری رجیستر که با `VERIFY` علامت خورده‌اند و پلاریته
  ورودی‌های IPM باید در راه‌اندازی مرحله ۱ تأیید شوند.
* ⚠️ بدون PFC: برای محصول تجاری، استاندارد هارمونیک (IEC 61000-3-2) و EMC باید جداگانه حل شوند.
* ⚠️ مقادیر [DS] در محاسبات (تلفات IGBT، Rth، آستانه CIN) نمونه‌اند؛ با دیتاشیت پارت نهایی جایگزین کنید.

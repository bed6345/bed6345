# ProtectionStones for Bedrock (EndStone)

ปลั๊กอินป้องกันที่ดิน (land protection) สไตล์ **ProtectionStones** สำหรับ
**Minecraft Bedrock** เขียนด้วย **EndStone C++ API v0.11.4**

ผู้เล่นเสกบล็อกพิเศษด้วยคำสั่ง แล้ววางลงไป → เกิดเขตป้องกันรอบบล็อกนั้นทันที
โดยบล็อกเป็นศูนย์กลางของเขต ทุบบล็อกศูนย์กลาง = ลบเขต ระบบป้องกันทั้งหมดเขียนเอง
(Bedrock ไม่มี WorldGuard)

---

## บล็อกป้องกัน 3 ชนิด

| บล็อก (vanilla) | ขนาดเขต | รัศมี | คำสั่งเสก |
| --- | --- | --- | --- |
| Iron Block | ~20×20 | 10 | `/ps get small` |
| Gold Block | ~50×50 | 25 | `/ps get medium` |
| Diamond Block | ~100×100 | 50 | `/ps get large` |

> เขตครอบทั้งแท่ง Y (จากต่ำสุดถึงสูงสุดของโลก) — ตรวจสอบเฉพาะแกน X/Z
> บล็อกป้องกันได้จากคำสั่งเสกเท่านั้น (เช็คสิทธิ์ผ่าน permission)

---

## คำสั่ง (`/ps`)

| คำสั่ง | คำอธิบาย |
| --- | --- |
| `/ps get <small\|medium\|large>` | เสกบล็อกป้องกัน |
| `/ps menu` | เปิดเมนู UI (เรียกใช้ได้โดยยืนในเขต) |
| `/ps delete` | ลบเขตที่ยืนอยู่ (มี form ยืนยัน) |
| `/ps add <ชื่อ> [member\|guest]` | เพิ่มสมาชิก (เฉพาะคนออนไลน์, default = member) |
| `/ps remove <ชื่อ>` | ลบสมาชิก |
| `/ps transfer <ชื่อ>` | โอนเขตให้ผู้เล่นอื่น |
| `/ps list` | ดูเขตทั้งหมดของตัวเอง + กดวาร์ป |
| `/ps bypass` | โหมดทะลุทุกเขต (ทีมงาน) |

นามแฝงคำสั่ง: `/protectionstones`, `/land`

---

## ระบบสมาชิก 2 ระดับ

เก็บเป็น **XUID** เป็น key หลัก (ชื่อไว้แสดงผล อัปเดตทุกครั้งที่ผู้เล่นออนไลน์)

- **member** — build ได้เต็ม
- **guest** — เข้าใช้งานได้ (ประตู/หีบ/ปุ่ม) แต่ build ไม่ได้

## Flags (ค่าเริ่มต้น = กันหมดเพื่อความปลอดภัย)

| Flag | ค่าเริ่มต้น | ผล |
| --- | --- | --- |
| `build` | เปิด | เฉพาะ member ที่สร้าง/ทำลายบล็อกได้ |
| `interact` | เปิด | member + guest ใช้ประตู/หีบ/ปุ่มได้ |
| `pvp` | ปิด | กัน PvP ในเขต |
| `explosion` | ปิด | กันบล็อกในเขตโดนระเบิด |
| `fire` | ปิด | กันไฟ/ของเหลวไหลเข้าเขต |
| `entity_protect` | เปิด | กันฆ่าสัตว์เลี้ยง/armor stand/item frame |
| `lava` | ปิด | **(สำรองไว้)** กันลาวาไหลเต็มรูปแบบ — ยังไม่ทำในเวอร์ชันนี้ |

**บล็อกศูนย์กลางถูกกันทุกอย่างที่จะทำลายมัน:** ระเบิด, ของเหลว/ไฟไหลทับ,
ลูกสูบดัน/ดึง, **ทราย/กรวดตกทับคอลัมน์ศูนย์กลาง** → ถูก cancel เสมอ
ลบได้ทางเดียวคือเจ้าของทุบเอง (เด้ง form ยืนยันกันทุบพลาด)

---

## เมนู UI (Bedrock Form)

เปิดด้วย `/ps menu` ขณะยืนในเขตของตัวเอง:

- **จัดการสมาชิก** — ดูรายชื่อ / กดเพื่อเปลี่ยนระดับ (member/guest) หรือลบ /
  **เพิ่มสมาชิกในเมนูได้เลย** (เลือกจาก dropdown ผู้เล่นออนไลน์ ไม่ต้องพิมพ์ชื่อ)
- **ตั้งค่า flag** — เปิด-ปิด PvP / explosion / fire / interact (ModalForm + toggle)
- **ดูข้อมูลเขต** — เจ้าของ / ขนาด / มิติ / พิกัดศูนย์กลาง / จำนวนสมาชิก
- **แสดงขอบเขต** — วาดเส้นขอบด้วย particle ชั่วคราว
- **ลบเขต** — MessageForm ยืนยัน

ทุกอย่างใน UI ยังทำผ่านคำสั่งพิมพ์ `/ps ...` ได้เป็นทางเลือกสำรอง

---

## ไฟล์ข้อมูล (สร้างอัตโนมัติใน data folder ของปลั๊กอิน)

| ไฟล์ | คำอธิบาย |
| --- | --- |
| `claims.json` | เขตทั้งหมด (id, เจ้าของ XUID, สมาชิก, มิติ, ศูนย์กลาง, ขนาด, flags) |
| `lang_th_TH.json` | ข้อความทั้งหมด (เริ่มเป็นภาษาไทย) — แก้ได้ไม่ต้องแตะโค้ด |
| `config.json` | `max_claims_per_player` (default 3), `border_particle`, `border_seconds`, `limit_permissions` (permission → จำนวน claim) |

ไฟล์ `lang/th_TH.json` ใน repo เป็นชุดข้อความตัวอย่างไว้อ้างอิง

---

## โครงสร้างโปรเจกต์

```
.
├── CMakeLists.txt                 # FetchContent ดึง EndStone v0.11.4 + nlohmann/json
├── include/protectionstones/
│   ├── claim.h                    # โครงสร้างข้อมูล Claim / flags / size (ไม่ผูก EndStone)
│   ├── claim_manager.h            # ข้อมูล + spatial index + JSON
│   ├── lang.h                     # ระบบข้อความหลายภาษา
│   ├── protection_listener.h      # event handler (hot path)
│   ├── ui_manager.h               # เมนู Form
│   └── plugin.h                   # main Plugin class
├── src/
│   ├── claim_manager.cpp
│   ├── lang.cpp
│   ├── protection_listener.cpp
│   ├── ui_manager.cpp
│   └── plugin.cpp                 # คำสั่ง + ENDSTONE_PLUGIN entry point
└── lang/th_TH.json
```

### ประสิทธิภาพ (รองรับ 50–100 คน)

- **Spatial index แบบ chunk-based:** `dimension → (chunk key → list ของ claim id)`
  การเช็คต่อ event เป็น O(1)-ish — แฮชตรงไปยัง chunk เดียว แล้วเทียบ AABB เฉพาะ
  claim ไม่กี่ตัวในนั้น ไม่วน loop ทุก claim
- **Event handler เบาที่สุด:** ไม่มี allocation บนเส้นทาง "ไม่ได้อยู่ในเขต"
  (lookup คืน pointer, early-return ทันที)
- การเช็ค overlap ตอนสร้างเขตใช้ AABB กล่องชนกล่องบนแกน X/Z

---

## วิธี Build

ต้องใช้ **Clang + libc++** (EndStone ออกแบบมาสำหรับ toolchain นี้ — ABI ต้องตรงกับ BDS
และ header ของ EndStone ใช้ `std::floorf` ที่ libstdc++ ของ GCC ไม่มี)

### สิ่งที่ต้องมี

- CMake ≥ 3.20
- Clang ≥ 16 พร้อม libc++ (`libc++-dev`, `libc++abi-dev`)
- Ninja (หรือ Make)
- อินเทอร์เน็ต (FetchContent จะดึง EndStone, fmt, expected-lite, nlohmann/json)

### Linux

```bash
# ติดตั้ง toolchain (Ubuntu/Debian)
sudo apt-get install -y clang libc++-dev libc++abi-dev cmake ninja-build

# configure + build
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-stdlib=libc++"

cmake --build build
```

ได้ไฟล์ผลลัพธ์: `build/endstone_protectionstones.so`

### Windows

ใช้ MSVC (Release หรือ RelWithDebInfo เท่านั้น — **ห้าม Debug**) ได้ไฟล์
`endstone_protectionstones.dll`

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

---

## วิธีติดตั้ง

1. ติดตั้ง EndStone บนเซิร์ฟเวอร์ Bedrock (BDS) ของคุณ — ดู https://endstone.dev
2. คัดลอกไฟล์ที่ build ได้ไปไว้ในโฟลเดอร์ `plugins/` ของเซิร์ฟเวอร์:
   - Linux: `endstone_protectionstones.so`
   - Windows: `endstone_protectionstones.dll`
3. รีสตาร์ทเซิร์ฟเวอร์ ปลั๊กอินจะสร้างไฟล์ config/lang/claims ให้อัตโนมัติ
4. แก้ข้อความได้ที่ `plugins/protectionstones/lang_th_TH.json`
   และตั้งค่าได้ที่ `plugins/protectionstones/config.json`

---

## สิทธิ์ (Permissions)

| Permission | ค่าเริ่มต้น | ผล |
| --- | --- | --- |
| `protectionstones.command` | ทุกคน | ใช้คำสั่ง `/ps` |
| `protectionstones.get.small` | ทุกคน | เสกบล็อกเล็ก |
| `protectionstones.get.medium` | ทุกคน | เสกบล็อกกลาง |
| `protectionstones.get.large` | ทุกคน | เสกบล็อกใหญ่ |
| `protectionstones.bypass` | OP | ทะลุทุกเขต (admin) |
| `protectionstones.limit.*` | (ตั้งใน config) | ปรับเพดานจำนวน claim ต่อคน เช่น `protectionstones.limit.vip` → 10 |

---

## ยังไม่ทำในเวอร์ชันนี้

- **ป้องกันลาวาไหลเต็มรูปแบบ** (เข้าเขต/ในเขต) — เผื่อ flag `lava` ไว้ในโครงสร้าง
  และโครง event (`BlockFromToEvent`) แล้ว ค่อยเปิดใช้ทีหลังได้
- **ไฟลามแบบเต็มเขต / จุดไฟไม่ติด** — EndStone v0.11.4 ไม่มี event ไฟ (ignite/spread)
  ให้ดักเลย จึงทำได้เท่าที่ event รองรับ: กันของเหลว/ไฟ **ไหลทับ** ศูนย์กลาง และกันระเบิด
  (บล็อกศูนย์กลางเป็นโลหะอยู่แล้วจึงไม่ติดไฟโดยธรรมชาติ) — ถ้าต้องการกันไฟทั้งเขต
  ต้องใช้ periodic scan ซึ่งมีต้นทุน CPU จึงยังไม่เปิดเป็นค่าเริ่มต้น

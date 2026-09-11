# IoT-Based Plant Stress Detection and Analysis via Acoustic Signals

ระบบตรวจจับความเครียดของพืชผ่านสัญญาณเสียง (Acoustic Signals) โดยใช้อุปกรณ์ IoT (Arduino) เก็บข้อมูลเสียงจาก **ต้นพลูด่าง (Epipremnum aureum)** ส่งขึ้น Google Sheet ผ่าน Google Apps Script เพื่อแสดงผลบน Dashboard แบบเรียลไทม์ พร้อมโมเดล AI (fine-tuned HuBERT) สำหรับจำแนกเสียงออกเป็น 3 คลาส ได้แก่:

- **Plant_Sounds** — เสียงที่เกิดจากสภาวะเครียดของพืชจากการขาดน้ำ
- **Normal** — เสียงในสภาวะปกติของพืช
- **Noise** — เสียงรบกวนทั่วไป (ไม่ใช่เสียงจากพืช)

โมเดลถูก deploy ไว้เป็น Gradio App บน Hugging Face Spaces

🔗 **ตัวอย่างเว็บที่ deploy แล้ว (Live Demo):** [HuBERT-Plant_Audio_Classifier](https://huggingface.co/spaces/NonSittinon/HuBERT-Plant_Audio_Classifier) — อัปโหลดไฟล์เสียง `.wav` ความยาวประมาณ 1 วินาที แล้วระบบจะบอกว่าเป็นเสียงพืชสภาวะเครียด เสียงพืชสภาวะปกติ หรือเสียงรบกวน พร้อมแสดงค่าความมั่นใจ (confidence) ของแต่ละคลาส

## 🌿 เกี่ยวกับชุดข้อมูล (Dataset & การเก็บข้อมูล)

ชุดข้อมูลเสียงในโปรเจกต์นี้ใช้พืชทดลองคือ **ต้นพลูด่าง (Epipremnum aureum)** จำนวน 2 ต้น:

- **ต้นที่ 1** — อยู่ในสภาวะปกติ (รดน้ำตามปกติ) → ใช้เป็นข้อมูลคลาส `Normal`
- **ต้นที่ 2** — อยู่ในสภาวะเครียดจากการขาดน้ำ (งดรดน้ำ) → ใช้เป็นข้อมูลคลาส `Plant_Sounds`

**วิธีเก็บข้อมูล:**
1. ทำ **กล่องเก็บเสียง (soundproof box)** ขึ้นเองเพื่อลดเสียงรบกวนจากภายนอกขณะบันทึก
2. ทำ **โพรงด้วยดินน้ำมัน** ติดบริเวณลำต้นของพืชแต่ละต้น เพื่อเป็นจุดติดตั้งเซนเซอร์/ไมโครโฟนสำหรับรับสัญญาณเสียงจากลำต้นโดยตรง
3. นำพืชแต่ละต้นเข้าไปวัดและบันทึกเสียงภายในกล่องเก็บเสียงที่เตรียมไว้ แยกไฟล์ตามคลาส (`Plant_Sounds` / `Normal`) ส่วนคลาส `Noise` เก็บจากเสียงรบกวนทั่วไปที่ไม่ใช่เสียงจากพืช
4. ไฟล์เสียงทั้งหมดเป็น `.wav` ความยาวสั้น (~1 วินาที/ไฟล์) เก็บไว้ในโฟลเดอร์ `data/Plant_Sounds/` แยกตามคลาส

## 📌 ภาพรวมระบบ (System Overview)

```
[ไมโครโฟน/เซนเซอร์] → [Arduino] → [Google Apps Script] → [Google Sheet] → [Dashboard]
                                                                              │
                                                                              ▼
                                              [HuBERT Model บน Hugging Face Spaces (app.py)]
                                                                              │
                                                                              ▼
                                                  ผลการจำแนกเสียง: Plant_Sounds / Normal / Noise
```

1. **Arduino** อ่านสัญญาณเสียงจากเซนเซอร์ที่ติดตั้งกับพืช แล้วส่งข้อมูลผ่าน HTTP request ไปยัง Web App ของ Google Apps Script
2. **Google Apps Script** รับข้อมูล บันทึกลง Google Sheet และให้บริการ Dashboard (HTML/CSS/JS) สำหรับแสดงผลแบบเรียลไทม์
3. **Dashboard (javascript.html)** เรียกใช้โมเดล AI ที่ deploy อยู่บน Hugging Face Space ผ่าน Gradio Client เพื่อจำแนกไฟล์เสียง
4. **AI Model (HuBERT)** ถูกเทรนจาก notebook ในโปรเจกต์นี้ แล้วนำไป deploy เป็น Gradio App (`app.py`) บน Hugging Face Spaces เพื่อจำแนกเสียงเป็น 3 คลาส

## 📂 โครงสร้างโปรเจกต์ (Repository Structure)

```
IoT-Based-Plant-Stress-Detection-and-Analysis-via-Acoustic-Signals/
├── arduino/                   # โค้ดฝั่งอุปกรณ์ IoT
│   └── *.ino                  # โค้ด Arduino สำหรับอ่านเซนเซอร์และส่งข้อมูลขึ้น Google Sheet
│
├── google-appscript/           # ฝั่ง Backend + Dashboard บน Google Apps Script
│   ├── code.gs                # รับข้อมูลจาก Arduino, จัดการ Google Sheet, เสิร์ฟหน้าเว็บ
│   ├── dashboard.html         # โครงหน้า Dashboard
│   ├── style.html             # สไตล์ (CSS) ของ Dashboard
│   └── javascript.html        # สคริปต์ฝั่ง Client สำหรับดึง/แสดงข้อมูลแบบเรียลไทม์
│
├── ai-model/
│   └── notebook/
│       └── HuBERT Audio Classifier: Plant_Sounds vs Normal vs Noise.ipynb   # Notebook เทรน + ทดสอบโมเดลด้วย Gradio
│
├── data/                       # ชุดข้อมูลเสียงสำหรับเทรน/ทดสอบโมเดล (3 คลาส)
│   └── Plant_Sounds/
│       ├── Plant_Sounds/        # ไฟล์เสียงพืชสภาวะเครียดจากการขาดน้ำ (.wav)
│       ├── Normal/               # ไฟล์เสียงพืชสภาวะปกติ (.wav)
│       └── Noise/                 # ไฟล์เสียงรบกวน (.wav)
│
├── model/                       # โมเดลที่เทรนเสร็จแล้ว (checkpoint/.pt)
│   └── *.pt                    # ไฟล์น้ำหนักโมเดล (จัดการผ่าน Git LFS เนื่องจากไฟล์มีขนาดใหญ่)
│
├── docs/                        # เอกสารประกอบ, ไดอะแกรม
│
├── app.py                      # สคริปต์ Gradio App — ใช้ deploy โมเดลบน Hugging Face Spaces (ดูตัวอย่างที่ deploy แล้วด้านล่าง)
├── requirements.txt             # รายชื่อ Python dependencies
├── .gitattributes                # กำหนดค่า Git LFS สำหรับไฟล์โมเดลขนาดใหญ่
└── readme.md
```

## ⚙️ การติดตั้งและใช้งาน (Setup)

### 1. Arduino (ฝั่งเก็บข้อมูล)

1. เปิดไฟล์ `.ino` ในโฟลเดอร์ `arduino/` ด้วย Arduino IDE
2. มองหาบรรทัดค่าคงที่ 3 ตัวนี้ที่อยู่ด้านบนสุดของไฟล์ (ก่อน `void setup()`) แล้วใส่ค่าของคุณลงในเครื่องหมายคำพูด:

   ```cpp
   const char* ssid     = "";   // <-- ใส่ชื่อ WiFi (SSID) ของคุณ
   const char* password = "";   // <-- ใส่รหัสผ่าน WiFi ของคุณ
   const char* APPS_SCRIPT_URL  = "";   // <-- ใส่ URL ของ Web App หลัง Deploy Google Apps Script
   ```

3. อัปโหลดโค้ดขึ้นบอร์ด แล้วเริ่มเก็บข้อมูลเสียง/เซนเซอร์ส่งขึ้น Google Sheet อัตโนมัติ ผ่าน `APPS_SCRIPT_URL`

> ⚠️ **ห้าม commit ค่าจริงของ `ssid`, `password`, `APPS_SCRIPT_URL` ขึ้น GitHub แบบ public repo** เพราะ `APPS_SCRIPT_URL` คือ endpoint ที่เขียนข้อมูลลง Google Sheet ได้ ถ้าหลุดคนอื่นยิง request ปลอมเข้ามาก่อกวนข้อมูลได้ ดูวิธีป้องกันในหัวข้อ [🔒 การจัดการข้อมูลลับ](#-การจัดการข้อมูลลับ-secrets) ด้านล่าง

### 2. Google Apps Script (Backend + Dashboard)

1. สร้างโปรเจกต์ Google Apps Script ใหม่ ผูกกับ Google Sheet ที่ต้องการเก็บข้อมูล
2. คัดลอกเนื้อหาจาก `google-appscript/code.gs`, `dashboard.html`, `style.html`, `javascript.html` เข้าไปในโปรเจกต์
3. ในไฟล์ `code.gs` ตั้งค่าตัวแปรต่อไปนี้ที่อยู่ด้านบนของไฟล์:

   ```javascript
   const SHEET_URL    = "..."; // <-- ใส่ URL ของ Google Sheet ที่จะใช้เก็บข้อมูล (เปิด Sheet แล้ว copy จาก address bar)
   const CONFIG_SHEET = "_config"; // ชื่อชีตย่อยที่ใช้เก็บค่า config ภายใน ไม่ต้องแก้ถ้าใช้ชื่อเดิม
   ```

4. Deploy เป็น **Web App** (Execute as: Me, Who has access: Anyone) แล้วนำ URL ที่ได้ไปใส่ในตัวแปร `APPS_SCRIPT_URL` ของโค้ด Arduino
5. เปิด URL ของ Web App เพื่อดู Dashboard แบบเรียลไทม์

### 3. Dashboard (javascript.html) — เชื่อมต่อ Hugging Face Space

ใน `google-appscript/javascript.html` มีส่วนที่เรียกใช้โมเดล AI ผ่าน Hugging Face Space (Gradio Client):

```javascript
// ใส่ Token ใน hf_token เพื่อยืนยันตัวตนขอโควต้าเพิ่ม
const client = await Client.connect('.....', { // <-- ใส่ชื่อ Hugging Face Space เช่น "username/space-name"
  hf_token: '....' // <-- นำ Token ที่ได้จาก Hugging Face มาวางที่นี่
});
```

- `Client.connect('...')` : ใส่ชื่อ Space บน Hugging Face ที่โฮสต์โมเดลของคุณ รูปแบบคือ `"<username>/<space-name>"`
- `hf_token: '...'` : นำ Access Token จากหน้า [Hugging Face Settings → Access Tokens](https://huggingface.co/settings/tokens) มาใส่ เพื่อเพิ่มโควต้าการเรียกใช้งาน

> 🚨 **คำเตือนสำคัญ:** `javascript.html` เป็นโค้ดฝั่ง Client ที่รันในเบราว์เซอร์ของทุกคนที่เปิด Dashboard — ถ้าใครกด "View Page Source" ก็จะเห็น `hf_token` ของคุณทันที **ไม่ควรฝัง Token จริงลงไปตรงๆ แล้ว push ขึ้น public repo** เพราะเท่ากับให้ทุกคนใช้โควต้า Hugging Face ของคุณได้ฟรี หรือถูกนำ Token ไปใช้ในทางที่ไม่ควร แนะนำให้:
> - ใช้ Token แบบ **read-only / fine-grained** ที่จำกัดสิทธิ์เฉพาะ Space นั้น (อย่าใช้ token สิทธิ์เต็ม)
> - หรือย้ายการเรียก Hugging Face ไปทำฝั่ง `code.gs` (Server-side) แทน แล้วเก็บ Token ไว้ใน [Script Properties](https://developers.google.com/apps-script/guides/properties) ของ Apps Script ซึ่งไม่ถูกมองเห็นจากฝั่ง Client
> - หากเผลอ commit Token ไปแล้ว ให้รีบไปที่ Hugging Face Settings แล้ว **Revoke Token เดิมทันที** และสร้างใหม่

### 4. AI Model — เทรนและทดสอบโมเดล HuBERT (Notebook)

```bash
pip install -r requirements.txt
```

เปิดและรันไฟล์ `ai-model/notebook/HuBERT Audio Classifier: Plant_Sounds vs Normal vs Noise.ipynb` เพื่อ:
- โหลดชุดข้อมูลเสียงจาก `data/Plant_Sounds/` ซึ่งแบ่งเป็น 3 คลาส: `Plant_Sounds` (เสียงพืชเครียดจากขาดน้ำ), `Normal` (เสียงพืชสภาวะปกติ), `Noise` (เสียงรบกวน)
- Fine-tune โมเดล HuBERT สำหรับงานจำแนกเสียง (audio classification) แบบ 3 คลาส
- บันทึกโมเดลที่เทรนเสร็จไว้ในโฟลเดอร์ `model/`
- **ทดสอบโมเดลผ่าน Gradio ได้ทันทีในตัว Notebook** — ในไฟล์เดียวกันนี้มีส่วนที่รัน Gradio interface ขึ้นมา ให้ลองอัปโหลดไฟล์เสียงจากโฟลเดอร์ `data/Plant_Sounds/Plant_Sounds/`, `data/Plant_Sounds/Normal/` หรือ `data/Plant_Sounds/Noise/` เข้าไปทดสอบ แล้วดูผลการจำแนก/ค่าความมั่นใจของแต่ละคลาสได้เลย โดยไม่ต้อง deploy ขึ้น Hugging Face Spaces ก่อน

### 5. Deploy โมเดลเป็น Gradio App บน Hugging Face Spaces (app.py)

`app.py` คือสคริปต์ Gradio App ที่ใช้ deploy โมเดลขึ้น **Hugging Face Spaces** เพื่อให้เรียกใช้งานผ่านเว็บหรือผ่าน Gradio Client จากที่อื่นได้ (เช่นจาก `javascript.html` ใน Dashboard)

ตัวอย่างที่ deploy ไว้แล้ว: 🔗 [https://huggingface.co/spaces/NonSittinon/HuBERT-Plant_Audio_Classifier](https://huggingface.co/spaces/NonSittinon/HuBERT-Plant_Audio_Classifier)

**ขั้นตอน deploy ของคุณเอง:**
1. สร้าง Space ใหม่บน Hugging Face เลือก SDK เป็น **Gradio**
2. อัปโหลด `app.py`, `requirements.txt` และไฟล์โมเดล (`model/*.pt`) ขึ้น Space นั้น
3. รอ Space build เสร็จ จะได้ URL รูปแบบ `https://huggingface.co/spaces/<username>/<space-name>` และ endpoint สำหรับ Gradio Client คือ `<username>/<space-name>`
4. นำชื่อ Space ไปใส่ในตัวแปร `Client.connect('...')` ที่ `google-appscript/javascript.html` (ดูหัวข้อก่อนหน้า)

## 🧠 เกี่ยวกับโมเดล

- **Base model:** HuBERT (Hidden-unit BERT) สำหรับงาน audio/speech representation
- **Task:** Multi-class classification (3 คลาส) — `Plant_Sounds` (เสียงเครียดจากขาดน้ำ) vs `Normal` (สภาวะปกติ) vs `Noise` (เสียงรบกวน)
- **ชุดข้อมูล:** ไฟล์เสียง `.wav` ความยาวสั้น (~1 วินาที) เก็บจากต้นพลูด่าง 2 ต้น (ปกติ 1 ต้น / ขาดน้ำ 1 ต้น) ในกล่องเก็บเสียง — ดูรายละเอียดที่หัวข้อ [🌿 เกี่ยวกับชุดข้อมูล](#-เกี่ยวกับชุดข้อมูล-dataset--การเก็บข้อมูล) ด้านบน
- **Demo ที่ deploy แล้ว:** [huggingface.co/spaces/NonSittinon/HuBERT-Plant_Audio_Classifier](https://huggingface.co/spaces/NonSittinon/HuBERT-Plant_Audio_Classifier) — อัปโหลดไฟล์เสียงแล้วดูผลจำแนกพร้อมค่าความมั่นใจของแต่ละคลาสได้ทันที

> ⚠️ ไฟล์โมเดล (`.pt`) มีขนาดใหญ่ (~300 MB) จึงจัดการผ่าน **Git LFS** — ก่อน clone repo ให้ติดตั้ง [Git LFS](https://git-lfs.com/) แล้วรัน `git lfs pull` เพื่อดึงไฟล์โมเดลลงมาให้ครบ

```bash
git lfs install
git clone https://github.com/ENGCE301-1-68/IoT-Based-Plant-Stress-Detection-and-Analysis-via-Acoustic-Signals.git
cd IoT-Based-Plant-Stress-Detection-and-Analysis-via-Acoustic-Signals
git lfs pull
```

## 🔒 การจัดการข้อมูลลับ (Secrets)

ก่อน push ขึ้น GitHub ให้ตรวจสอบว่าไม่มีค่าจริงของสิ่งต่อไปนี้หลงเหลืออยู่ในโค้ด:

| ไฟล์ | ตัวแปร | ความเสี่ยงถ้าหลุด |
|---|---|---|
| `arduino/*.ino` | `ssid`, `password` | คนอื่นรู้รหัส WiFi บ้าน/ห้องแล็บ |
| `arduino/*.ino` | `APPS_SCRIPT_URL` | คนอื่นยิงข้อมูลปลอมเข้า Google Sheet ได้ |
| `google-appscript/code.gs` | `SHEET_URL` | คนอื่นเข้าถึง/แก้ไข Google Sheet ได้ (ถ้า Sheet เปิดสิทธิ์กว้าง) |
| `google-appscript/javascript.html` | `hf_token` | คนอื่นใช้โควต้า/บัญชี Hugging Face ของคุณได้ |

**แนวทางแก้ก่อนอัปโหลดจริง:**

1. เก็บค่าเหล่านี้ไว้แยกในไฟล์ตัวอย่าง เช่น `arduino/secrets.example.h` แล้วให้ไฟล์จริง `secrets.h` (ที่มีค่าจริง) อยู่ใน `.gitignore` — ในไฟล์ `.ino` เปลี่ยนไปใช้ `#include "secrets.h"` แทนการ hardcode
2. สำหรับ `hf_token` ที่ต้องอยู่บนหน้าเว็บ (client-side) ให้ใช้ Token สิทธิ์จำกัด (fine-grained, read-only) เท่านั้น อย่าใช้ Token สิทธิ์เต็มบัญชี
3. เพิ่มไฟล์ตัวอย่างต่อไปนี้ลงใน `.gitignore`:

   ```
   arduino/secrets.h
   .env
   *.token
   ```

4. ถ้า commit ค่าจริงไปแล้วก่อนหน้านี้ (แม้จะลบออกในคอมมิตหลัง) ค่านั้นยังอยู่ใน **git history** ต้อง revoke/เปลี่ยนรหัสผ่าน/token ใหม่เสมอ ไม่ใช่แค่ลบออกจากไฟล์ล่าสุด

## 🛠️ Tech Stack

| ส่วนประกอบ | เทคโนโลยี |
|---|---|
| Firmware/IoT | Arduino (C/C++) |
| Data Pipeline & Dashboard | Google Apps Script, HTML/CSS/JavaScript, Google Sheets |
| AI Model | Python, PyTorch, HuggingFace Transformers (HuBERT) |
| Model Demo/Deployment | Gradio, Hugging Face Spaces |
| Model Storage | Git LFS |

## 💡 ข้อเสนอแนะ

โปรเจกต์นี้เป็นการใช้อุปกรณ์ที่มีราคาถูก อุปกรณ์ที่ใช้ในระบบ เช่น ไมโครโฟนดิจิทัล และเซนเซอร์ตรวจวัดอุณหภูมิและความชื้น เป็นอุปกรณ์เกรดดีกว่าทั่วไปนิดนึง แต่ก็ยังมีข้อจำกัดด้านความไว (Sensitivity) และระดับความแม่นยำ หากเปลี่ยนไปใช้อุปกรณ์เซนเซอร์ที่มีมาตรฐานสูงขึ้นหรืออุปกรณ์เกรดใช้ในงานวิจัย จะช่วยเพิ่มความแม่นยำในการจับสัญญาณเสียงความถี่ต่ำ และลดความคลาดเคลื่อนของข้อมูลจากสภาพแวดล้อม ส่งผลให้ผลการวิเคราะห์มีความน่าเชื่อถือยิ่งขึ้น

## 👥 ผู้จัดทำ

1.**Mr. Sittinon	  Yongyutwichai**
2.**Mr. Benjarong	  Kanthajai**
3.**Mr. Norrapat	  Supa**


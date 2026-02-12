# 🛡️ EXTERNAL RUST AI-KERNEL
---

### 💡 Project Overview
This is an external source for Rust, developed with AI assistance to implement **Stealth Kernel Communication**. The goal was to build a bypass that avoids common detection vectors used by EAC.

---

### 📺 Video Demonstration
Watch the setup and gameplay directly here:

[![Watch the video](https://img.youtube.com/vi/YOUR_VIDEO_ID/0.jpg)](https://www.youtube.com/watch?v=YOUR_VIDEO_ID)

*If the player doesn't load, click the image above.*

---

### 🛠️ Technical Specifications

#### 🟢 Kernel Driver
* **Trace Cleaning:** Wipes PiDDB and MmUnloadedDrivers from kernel memory.
* **Stealth:** Unlinks and zeros out the Driver Object to remain invisible in system lists.
* **PTE Hooking:** Uses Page Table Entry hooks for physical execution redirection.
* **Direct R/W:** Reads physical memory directly to bypass virtual memory monitoring.

#### 🔵 User Mode
* **Interface:** Clean ImGui overlay for feature toggling.
* **Visuals:** Box, Sleepers, and Name ESP, Health.
* **Offsets:** Latest offsets are pre-configured in the source.

---

### 🚀 Quick Start Guide
1. **Compile:** Build the Driver solution first, then the User Mode.
2. **Cleanup:** Run the `.bat` script included to kill any EAC remnants.
3. **Load:** Use `kdmapper` to load the driver into memory. 
4. **Warning:** Test in a **VM** first to ensure no BSOD occurs on your main OS.
5. **Run:** Launch Rust, then start the compiled EXE.

---

### 💬 Discord & Community
Need help or want to discuss the project?
> **Join here:** [https://discord.gg/qcmr7Kh27C](https://discord.gg/qcmr7Kh27C)

---

> **Disclaimer:** This project is for educational purposes only. I am not responsible for any bans or system damage.

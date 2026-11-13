# PolarFire SoC Bare-Metal lwIP Network Application

This project is a bare-metal C application demonstrating Ethernet networking (lwIP 2.x) on the **PolarFire SoC Discovery Kit (MPFS095T)**. It runs entirely without an RTOS (`NO_SYS=1`) and features a DHCP client, an ICMP Ping responder, a static HTTP Web Server (TCP/80) with CGI handling, and a TFTP RAM-Disk Server (UDP/69).

## Hardware & Peripheral Routing
* **Target Board**: PolarFire SoC Discovery Kit
* **Ethernet MAC**: GEM0
* **Ethernet PHY**: VSC8221 (over Bank 5 SGMII / TBI interface)
* **MDIO Address**: `11`
* **HSS Boot Console**: `MMUART0` (typically `/dev/ttyUSB0` or `/dev/ttyUSB1`)
* **Application Stdio**: `MMUART4` (typically `/dev/ttyUSB2` or `/dev/ttyUSB3`)

---

## 1. Critical Configuration Settings

Getting the MAC, PHY, and lwIP stack to work cooperatively requires specific configurations across several files. 

### A. Board Build Flags (`boards/mpfs-disco-kit/board.mk`)
Ensure the compiler builds for the Discovery Kit and utilizes a single-queue TX structure:
```makefile
BOARD_CFLAGS += -DBOARD_MPFS_DISCO_KIT \
                -DMPFS_DISCOVERY_KIT \
                -DTARGET_DISCOVERY_KIT \
                -DTARGET_G5_SOC \
                -DMSS_MAC_SIMPLE_TX_QUEUE \
                -DGEM0_MSS_MDIO
```

### B. Board Hardware Config (`boards/mpfs-disco-kit/board_config.h`)
The Discovery Kit uses a VSC8221 PHY connected via the TBI interface. The MAC interrupt must route to the PLIC handler.
```c
#define APP_MAC_IRQn            MAC0_INT_U54_INT
#define APP_MAC_POLL_ISR()      ((void)PLIC_mac0_int_IRQHandler())
#define APP_PHY_ADDR            11U   /* PHY_VSC8221_MDIO_ADDR */
#define APP_PCS_PHY_ADDR        SGMII_MDIO_ADDR
#define APP_PHY_INTERFACE       TBI   /* Bank 5 SGMII TBI Mode */
#define APP_PHY_TYPE            MSS_MAC_DEV_PHY_VSC8221
```

### C. Clock & Reset Initialization (`boards/mpfs-disco-kit/board_init.c`)
Before calling `MSS_MAC_init()`, the system registers must enable the MAC and UART clocks, and the PLIC must be initialized to accept interrupts.
```c
SYSREG->SOFT_RESET_CR   = 0U;
SYSREG->SUBBLK_CLOCK_CR = 0xFFFFFFFFUL;
(void)mss_config_clk_rst(MSS_PERIPH_MMUART4, MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
(void)mss_config_clk_rst(MSS_PERIPH_MAC0,    MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
PLIC_init();
__enable_irq();
```

### D. MAC Software Config (`mss_ethernet_mac_sw_cfg.h`)
To prevent dropped packets during parallel TCP requests (like a web browser requesting `/` and `/favicon.ico` simultaneously), the RX ring must be sufficiently large. 
```c
#define MSS_MAC_RX_RING_SIZE (32U)  /* Do NOT use 4U */
#define MSS_MAC_TX_RING_SIZE (16U)
```

### E. lwIP Options (`middleware/net_stack/lwipopts.h`)
TFTP and DHCP require enough UDP PCBs and System Timeout slots to function simultaneously.
```c
#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_SYS_TIMEOUT            16  /* Prevents TFTP allocation failures */
```

---

## 2. Building & Booting

1. **Build the DDR Target**:
   ```bash
   make BOARD=mpfs-disco-kit clean ddr
   ```
2. **Open the Application Console (MMUART4)**:
   In a separate terminal, open the application's UART port.
   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSB2 115200
   ```
3. **Boot via HSS**:
   Trigger the HSS payload transfer over MMUART0.
   ```bash
   make BOARD=mpfs-disco-kit ddr-boot
   ```

*(Wait for the board to boot. In your `miniterm` console, you will see the hardware init sequence, link auto-negotiation, and either a DHCP IP assignment or a fallback to the static `192.168.20.50`.)*

---

## 3. Testing the Network Services

Once the application console prints `[DHCP] Lease acquired!` or `applied static IPv4`, note the IP address and run the following tests from your host PC.

### A. ICMP Ping Test
Verify basic Layer 3 connectivity:
```bash
ping -c 4 <board-ip>
```
*Note: Press `s` in the UART console to watch the `RX` and `TX` packet counters increment.*

### B. HTTP Web Server (TCP Port 80)
Test the web server handshake and payload delivery using `curl`:
```bash
curl -v http://<board-ip>/
```
Test the simulated CGI endpoint (this will trigger a log message on the board's UART console):
```bash
curl -i "http://<board-ip>/ctrl.cgi?cmd=led_toggle"
```
*You can also open `http://<board-ip>/` in a standard desktop web browser.*

### C. TFTP RAM-Disk Server (UDP Port 69)
The TFTP server strictly requires **Binary/Octet** mode. The default Linux `tftp` client uses `netascii`, which will result in a timeout if not overridden.

Test uploading a file (`put`):
```bash
tftp -m octet <board-ip> -c put Makefile
```
Test downloading the file back (`get`):
```bash
tftp -m octet <board-ip> -c get Makefile /tmp/Makefile.rx
diff -s Makefile /tmp/Makefile.rx
```
*Note: Press `t` in the UART console to view the currently stored file in the RAM-disk buffer and its size in bytes.*
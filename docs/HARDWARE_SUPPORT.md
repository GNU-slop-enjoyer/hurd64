# Hardware support (target machine)

| Device | ID | Hurd today | Firmware |
|---|---|---|---|
| CPU Ryzen 9 9950X, 16c/32t | | boots; SMP works only with smpctl, hangs under fs load | none |
| NVMe Netac NV3000 (target) | 1f40:1202 | no driver (rumpdisk: AHCI/IDE) | none |
| SATA AMD 600 | 1022:43f6 | AHCI in rumpdisk (untested here) | none |
| RTL8125 2.5GbE | 10ec:8125 | no driver (NetBSD has rge(4)) | rge needs none on NetBSD; to check |
| Intel AX210 Wi-Fi | 8086:2725 | no driver | needs nonfree iwlwifi firmware |
| RX 9070 (RDNA4) | 1002:7550 | no display stack | needs nonfree amdgpu firmware |
| Granite Ridge iGPU (RDNA2) | 1002:13c0 | no display stack | needs nonfree amdgpu firmware |
| xHCI USB | 1022:15b6/7/8, 43f7 | rumpusbdisk only | none |
| Firmware | UEFI only | GNU Mach boots via multiboot; UEFI boot untested | |

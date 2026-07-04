CPU Information v0.1
====================
Developed by ExEintel
License: MIT

DESCRIPTION
-----------
CPU Information is a console utility for displaying detailed system
hardware and software information on Windows. It supports colored
output and can export reports to text files.

SYSTEM REQUIREMENTS
-------------------
- Windows 7 / 8 / 10 / 11
- For C version: No additional dependencies
- For batch version: WMIC (built-in on Windows)

FILES
-----
cpuinformation32.exe   - 32-bit compiled version
cpuinformation64.exe   - 64-bit compiled version
cpuinformation.cmd     - Portable batch script version
source/main.c          - C source code
Readme.txt             - This file

USAGE
-----
  cpuinformation.exe [options]
  cpuinformation.cmd [options]

OPTIONS
-------
  -i, -all         Show all system information
  -cpu             CPU information
  -gpu             GPU information
  -ram             RAM information
  -rom             Storage (disk) information
  -mb              Motherboard information
  -os              Operating system information
  -temp            Component temperatures
  -bench           CPU performance benchmark
  -export <file>   Export output to text file
  -h, --help       Show this help
  -v, --version    Show version

EXAMPLES
--------
  cpuinformation64.exe -i
  cpuinformation64.exe -cpu -gpu -ram
  cpuinformation.cmd -all
  cpuinformation64.exe -temp -bench
  cpuinformation.exe -export report.txt
  cpuinformation.exe -h

COLOR CODING
------------
  CPU:  Intel -> Blue, AMD -> Red
  GPU:  NVIDIA -> Green, AMD -> Red, Intel -> Blue
  RAM:  Yellow
  ROM:  Cyan
  MB:   Purple
  OS:   White
  Temp: Red/Orange (based on temperature)

BUILD FROM SOURCE
-----------------
Requirements: MinGW-w64 GCC

32-bit:
  gcc -m32 -o cpuinformation32.exe source/main.c -lole32 -loleaut32 -O2

64-bit:
  gcc -m64 -o cpuinformation64.exe source/main.c -lole32 -loleaut32 -O2

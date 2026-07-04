@echo off
setlocal enabledelayedexpansion

title CPU Information v0.1
chcp 65001 >nul 2>&1

set "VERSION=0.1"
set "HAS_ACTION=0"

:parse_args
if "%~1"=="" goto :check_action
if /i "%~1"=="-h" goto :help
if /i "%~1"=="--help" goto :help
if /i "%~1"=="-v" goto :version
if /i "%~1"=="--version" goto :version
if /i "%~1"=="-i" goto :all
if /i "%~1"=="-all" goto :all
if /i "%~1"=="-cpu" goto :cpu
if /i "%~1"=="-gpu" goto :gpu
if /i "%~1"=="-ram" goto :ram
if /i "%~1"=="-rom" goto :rom
if /i "%~1"=="-mb" goto :mb
if /i "%~1"=="-os" goto :os
if /i "%~1"=="-temp" goto :temp
if /i "%~1"=="-bench" goto :bench
if /i "%~1"=="-export" (
    set "EXPORT_FILE=%~2"
    shift
    shift
    goto :parse_args
)
echo Unknown option: %~1
shift
goto :parse_args

:check_action
if "%HAS_ACTION%"=="0" (
    call :print_help
)
goto :end

:help
call :print_help
goto :end

:version
echo CPU Information v%VERSION%
echo Developed by ExEintel
echo Platform: Windows
goto :end

:all
set "HAS_ACTION=1"
call :cpu_section
call :gpu_section
call :ram_section
call :rom_section
call :mb_section
call :os_section
call :temp_section
goto :end

:cpu
set "HAS_ACTION=1"
call :cpu_section
goto :end

:gpu
set "HAS_ACTION=1"
call :gpu_section
goto :end

:ram
set "HAS_ACTION=1"
call :ram_section
goto :end

:rom
set "HAS_ACTION=1"
call :rom_section
goto :end

:mb
set "HAS_ACTION=1"
call :mb_section
goto :end

:os
set "HAS_ACTION=1"
call :os_section
goto :end

:temp
set "HAS_ACTION=1"
call :temp_section
goto :end

:bench
set "HAS_ACTION=1"
call :bench_section
goto :end

rem ──── HEADER ────
:header
echo.
echo ============================================================
echo   %~1
echo ============================================================
goto :eof

rem ──── CPU ────
:cpu_section
call :header "CPU Information"
for /f "skip=1" %%a in ('wmic cpu get Name 2^>nul') do if not "%%a"=="" echo   Name:              %%a&goto :cpu_name
:cpu_name
for /f "skip=1" %%a in ('wmic cpu get Manufacturer 2^>nul') do if not "%%a"=="" echo   Manufacturer:      %%a&goto :cpu_man
:cpu_man
for /f "skip=1" %%a in ('wmic cpu get NumberOfCores 2^>nul') do if not "%%a"=="" echo   Cores:             %%a&goto :cpu_cores
:cpu_cores
for /f "skip=1" %%a in ('wmic cpu get NumberOfLogicalProcessors 2^>nul') do if not "%%a"=="" echo   Logical Processors:%%a&goto :cpu_threads
:cpu_threads
for /f "skip=1" %%a in ('wmic cpu get MaxClockSpeed 2^>nul') do if not "%%a"=="" echo   Max Clock:         %%a MHz&goto :cpu_max
:cpu_max
for /f "skip=1" %%a in ('wmic cpu get CurrentClockSpeed 2^>nul') do if not "%%a"=="" echo   Current Clock:     %%a MHz&goto :cpu_cur
:cpu_cur
for /f "skip=1" %%a in ('wmic cpu get L2CacheSize 2^>nul') do if not "%%a"=="" echo   L2 Cache:          %%a KB&goto :cpu_l2
:cpu_l2
for /f "skip=1" %%a in ('wmic cpu get L3CacheSize 2^>nul') do if not "%%a"=="" echo   L3 Cache:          %%a KB&goto :cpu_l3
:cpu_l3
echo   TDP:               N/A ^(not exposed via WMI^)
goto :eof

rem ──── GPU ────
:gpu_section
call :header "GPU Information"
set "gpu_count=0"
for /f "skip=1" %%a in ('wmic path Win32_VideoController get Name 2^>nul') do (
    if not "%%a"=="" (
        set /a gpu_count+=1
        echo   GPU !gpu_count!:
        echo     Model:       %%a
    )
)
if "!gpu_count!"=="0" echo   No GPU found.
goto :eof

rem ──── RAM ────
:ram_section
call :header "RAM Information"
set "stick_count=0"
set "total_ram=0"
for /f "skip=1" %%a in ('wmic memorychip get Capacity 2^>nul') do (
    if not "%%a"=="" (
        set /a stick_count+=1
        set /a total_ram+=%%a
    )
)
if "!stick_count!"=="0" (
    echo   No RAM information found.
    for /f "skip=1" %%a in ('wmic os get TotalVisibleMemorySize 2^>nul') do (
        if not "%%a"=="" (
            set /a total_mb=%%a / 1024
            echo   Total RAM: !total_mb! MB
            goto :eof
        )
    )
    goto :eof
)
for /f "skip=1" %%a in ('wmic memorychip get Speed 2^>nul') do if not "%%a"=="" (
    if not "%%a"=="Speed" echo   Speed:     %%a MHz
    goto :ram_speed
)
:ram_speed
for /f "skip=1" %%a in ('wmic memorychip get SMBIOSMemoryType 2^>nul') do if not "%%a"=="" (
    call :ddr_type %%a
) else (
    echo   Type:      Unknown
)
echo   Modules:   !stick_count!
set /a total_gb=!total_ram! / 1073741824
echo   Total:     !total_gb! GB
goto :eof

:ddr_type
if "%1"=="20" echo   Type: DDR
if "%1"=="21" echo   Type: DDR2
if "%1"=="24" echo   Type: DDR3
if "%1"=="26" echo   Type: DDR4
if "%1"=="34" echo   Type: DDR5
if "%1"=="" echo   Type: Unknown
goto :eof

rem ──── ROM ────
:rom_section
call :header "Storage Information"
echo   Physical Drives:
set "disk_count=0"
for /f "skip=1" %%a in ('wmic diskdrive get Model 2^>nul') do (
    if not "%%a"=="" (
        set /a disk_count+=1
        echo   Disk !disk_count!: %%a
    )
)
if "!disk_count!"=="0" echo   No disks found.
echo.
echo   Volumes:
echo   Drive  Type        Total       Used        Free        Use%
for /f "skip=1" %%a in ('wmic logicaldisk get DeviceID^,Size^,FreeSpace^,FileSystem 2^>nul') do (
    echo   %%a
)
goto :eof

rem ──── Motherboard ────
:mb_section
call :header "Motherboard Information"
for /f "skip=1" %%a in ('wmic baseboard get Manufacturer 2^>nul') do if not "%%a"=="" echo   Manufacturer: %%a&goto :mb_man
:mb_man
for /f "skip=1" %%a in ('wmic baseboard get Product 2^>nul') do if not "%%a"=="" echo   Model:        %%a&goto :mb_prod
:mb_prod
for /f "skip=1" %%a in ('wmic baseboard get Version 2^>nul') do if not "%%a"=="" echo   Version:      %%a&goto :mb_ver
:mb_ver
for /f "skip=1" %%a in ('wmic bios get SMBIOSBIOSVersion 2^>nul') do if not "%%a"=="" echo   BIOS Version: %%a&goto :mb_bios
:mb_bios
for /f "skip=1" %%a in ('wmic cpu get SocketDesignation 2^>nul') do if not "%%a"=="" echo   Socket:       %%a&goto :mb_sock
:mb_sock
goto :eof

rem ──── OS ────
:os_section
call :header "Operating System Information"
for /f "skip=1" %%a in ('wmic os get Caption 2^>nul') do if not "%%a"=="" echo   OS Name:     %%a&goto :os_cap
:os_cap
for /f "skip=1" %%a in ('wmic os get Version 2^>nul') do if not "%%a"=="" echo   Version:     %%a&goto :os_ver
:os_ver
for /f "skip=1" %%a in ('wmic os get BuildNumber 2^>nul') do if not "%%a"=="" echo   Build:       %%a&goto :os_build
:os_build
for /f "skip=1" %%a in ('wmic os get OSArchitecture 2^>nul') do if not "%%a"=="" echo   Arch:        %%a&goto :os_arch
:os_arch
echo   Computer:    %COMPUTERNAME%
echo   User:        %USERNAME%
goto :eof

rem ──── Temperature ────
:temp_section
call :header "Temperature Information"
echo   Temperature monitoring is limited in batch mode.
echo   Use the C version (-temp) for detailed sensor readings.
for /f "skip=1" %%a in ('wmic /namespace:\\root\wmi path MSAcpi_ThermalZoneTemperature get CurrentTemperature 2^>nul') do (
    if not "%%a"=="" (
        if not "%%a"=="CurrentTemperature" (
            set /a temp_c=%%a/10-273
            echo   CPU Temperature: !temp_c! C
            goto :temp_done
        )
    )
)
echo   No temperature data available.
:temp_done
goto :eof

rem ──── Benchmark ────
:bench_section
call :header "CPU Benchmark"
echo   Simple CPU benchmark...
echo   Note: For full benchmark accuracy, use the C version.
set "start=%time%"
set "count=0"
for /l %%i in (2,1,50000) do (
    set "isprime=1"
    for /l %%j in (2,1,%%i) do (
        set /a "rem=%%i %%%% j" 2>nul
        if !rem!==0 (
            if %%j neq %%i set "isprime=0"
        )
    )
    if !isprime!==1 set /a count+=1
)
set "end=%time%"
echo   Primes found: !count! ^(up to 50000^)
echo   Note: Batch benchmark is slow; use -bench with C version.
goto :eof

rem ──── Print Help ────
:print_help
echo CPU Information v%VERSION% - Console System Information Utility
echo Developed by ExEintel
echo.
echo Usage:
echo   cpuinformation.cmd [options]
echo.
echo Options:
echo   -i, -all         Show all system information
echo   -cpu             CPU information
echo   -gpu             GPU information
echo   -ram             RAM information
echo   -rom             Storage (disk) information
echo   -mb              Motherboard information
echo   -os              Operating system information
echo   -temp            Component temperatures
echo   -bench           CPU performance benchmark
echo   -export ^<file^>   Export output to text file
echo   -h, --help       Show this help
echo   -v, --version    Show version
echo.
echo Examples:
echo   cpuinformation.cmd -i
echo   cpuinformation.cmd -cpu -gpu -ram
echo   cpuinformation.cmd -temp -bench
echo   cpuinformation.cmd -export report.txt
goto :eof

:end
if not "%EXPORT_FILE%"=="" (
    echo.
    echo Report exported to %EXPORT_FILE%
)
endlocal

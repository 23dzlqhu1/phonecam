# setup_virtual_camera.ps1
# Create a virtual camera device using SetupAPI (no driver signing needed)
# Must run as Administrator

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public class SetupAPI {
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern IntPtr SetupDiCreateDeviceInfoList(IntPtr ClassGuid, IntPtr hwndParent);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern bool SetupDiCreateDeviceInfo(
        IntPtr DeviceInfoSet,
        string DeviceName,
        ref Guid ClassGuid,
        string DeviceDescription,
        IntPtr hwndParent,
        uint CreationFlags,
        IntPtr DeviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern bool SetupDiSetDeviceRegistryProperty(
        IntPtr DeviceInfoSet,
        IntPtr DeviceInfoData,
        uint Property,
        byte[] PropertyBuffer,
        uint PropertyBufferSize);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern bool SetupDiCallClassInstaller(
        uint InstallFunction,
        IntPtr DeviceInfoSet,
        IntPtr DeviceInfoData);

    public const uint DICD_GENERATE_ID = 0x00000001;
    public const uint DIF_REGISTERDEVICE = 0x00000019;
    public const uint SPDRP_HARDWAREID = 0x00000001;
    public const uint SPDRP_COMPATIBLEIDS = 0x00000002;
    public const uint SPDRP_DEVICEDESC = 0x00000000;
    public const uint SPDRP_MFG = 0x0000000B;
    public const uint SPDRP_FRIENDLYNAME = 0x0000000C;
}
"@

$cameraClassGuid = [guid]"{ca3e7ab9-b4c3-4ae6-8251-579ef933890f}"

Write-Output "=== Creating Virtual Camera Device ==="

# 1. Create device info list for Camera class
$devInfoSet = [SetupAPI]::SetupDiCreateDeviceInfoList([IntPtr]::Zero, [IntPtr]::Zero)
if ($devInfoSet -eq [IntPtr]::Zero -or $devInfoSet -eq [IntPtr]::new(-1)) {
    Write-Error "SetupDiCreateDeviceInfoList failed: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    exit 1
}

try {
    # 2. Create device info
    $hardwareId = "root\PhoneCamV0"
    $deviceName = "PhoneCam Camera"
    
    $result = [SetupAPI]::SetupDiCreateDeviceInfo(
        $devInfoSet,
        $deviceName,
        [ref]$cameraClassGuid,
        "PhoneCam Virtual Camera",
        [IntPtr]::Zero,
        [SetupAPI]::DICD_GENERATE_ID,
        [IntPtr]::Zero)

    if (-not $result) {
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Error "SetupDiCreateDeviceInfo failed: $err"
        exit 1
    }
    Write-Output "  Created device info"

    # 3. Set hardware ID
    $hwIdBytes = [System.Text.Encoding]::Unicode.GetBytes($hardwareId + "`0`0")
    $result = [SetupAPI]::SetupDiSetDeviceRegistryProperty(
        $devInfoSet,
        [IntPtr]::Zero,
        [SetupAPI]::SPDRP_HARDWAREID,
        $hwIdBytes,
        [uint32]$hwIdBytes.Length)

    if (-not $result) {
        Write-Warning "SetDeviceRegistryProperty(HARDWAREID) failed: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    } else {
        Write-Output "  Set HardwareID: $hardwareId"
    }

    # 4. Register the device
    $result = [SetupAPI]::SetupDiCallClassInstaller(
        [SetupAPI]::DIF_REGISTERDEVICE,
        $devInfoSet,
        [IntPtr]::Zero)

    if (-not $result) {
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Error "SetupDiCallClassInstaller(DIF_REGISTERDEVICE) failed: $err"
        exit 1
    }
    Write-Output "  Device registered successfully!"

} finally {
    [SetupAPI]::SetupDiDestroyDeviceInfoList($devInfoSet) | Out-Null
}

Write-Output ""
Write-Output "=== Done ==="
Write-Output "PhoneCam Camera should now appear as a Camera device."
Write-Output "Check Windows Camera settings or Device Manager."

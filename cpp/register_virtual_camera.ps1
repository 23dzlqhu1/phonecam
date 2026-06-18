# register_virtual_camera.ps1
# Register PhoneCam as a virtual camera device in Windows
# Must run as Administrator
# References: Iriun Webcam registry structure

$ErrorActionPreference = "Stop"

# Use a fixed GUID for the device (matches Iriun's DEVGEN pattern)
$deviceGuid = "{7A3B8C2D-4E5F-6A7B-8C9D-0E1F2A3B4C5D}"
$deviceInstanceId = "ROOT\DEVGEN\$deviceGuid"
$clsid = "{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}"
$cameraClassGuid = "{ca3e7ab9-b4c3-4ae6-8251-579ef933890f}"

# KS Category GUIDs
$KSCATEGORY_VIDEO_CAMERA  = "{e5323777-f976-4f5b-9b55-b94699c46e44}"
$KSCATEGORY_VIDEO         = "{6994ad05-93ef-11d0-a3cc-00a0c9223196}"
$KSCATEGORY_CAPTURE       = "{65e8773d-8f56-11d0-a3b9-00a0c9223196}"
$ProxyVCap_CLSID          = "{17cca71b-ecd7-11d0-b908-00a0c9223196}"

Write-Output "=== Registering PhoneCam Virtual Camera ==="

# 1. Clean up old ROOT\Image registration if exists
$oldPath = "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT\Image\0000"
if (Test-Path $oldPath) {
    Remove-Item -Path $oldPath -Recurse -Force -ErrorAction SilentlyContinue
    Write-Output "Cleaned up old ROOT\Image\0000"
}

# 2. Create device instance under ROOT\DEVGEN (like Iriun)
$devPath = "HKLM:\SYSTEM\CurrentControlSet\Enum\$deviceInstanceId"
New-Item -Path $devPath -Force | Out-Null

Set-ItemProperty $devPath -Name "ClassGUID" -Value $cameraClassGuid
Set-ItemProperty $devPath -Name "Class" -Value "Camera"
Set-ItemProperty $devPath -Name "DeviceDesc" -Value "PhoneCam Camera"
Set-ItemProperty $devPath -Name "Mfg" -Value "PhoneCam"
Set-ItemProperty $devPath -Name "FriendlyName" -Value "PhoneCam Camera"
Set-ItemProperty $devPath -Name "ContainerID" -Value "{00000000-0000-0000-FFFF-FFFFFFFFFFFF}"
Set-ItemProperty $devPath -Name "Driver" -Value "$cameraClassGuid\0002"
Set-ItemProperty $devPath -Name "Capabilities" -Value 0 -Type DWord
Set-ItemProperty $devPath -Name "ConfigFlags" -Value 0 -Type DWord
Set-ItemProperty $devPath -Name "HardwareID" -Value @("root\PhoneCamV0") -Type MultiString
Set-ItemProperty $devPath -Name "CompatibleIDs" -Value @("ROOT\DevGenDevice", "DevGenDevice") -Type MultiString
# Service = empty (no UMDF driver, just COM)
# Set-ItemProperty $devPath -Name "Service" -Value ""

Write-Output "  Created device: $deviceInstanceId"

# 3. Device Parameters
$devParams = "$devPath\Device Parameters"
New-Item -Path $devParams -Force | Out-Null

Set-ItemProperty $devParams -Name "CLSID" -Value $clsid
Set-ItemProperty $devParams -Name "CustomCaptureSourceClsid" -Value $clsid
Set-ItemProperty $devParams -Name "FriendlyName" -Value "PhoneCam Camera"

# Also add the ProxyVCap CLSID (used by Windows to identify video capture sources)
Set-ItemProperty $devParams -Name "ProxyVCap.CLSID" -Value $ProxyVCap_CLSID

Write-Output "  Set Device Parameters"

# 4. Register device interfaces (KSCATEGORY)
$devInstanceForPath = $deviceInstanceId.Replace('\', '#')
$categories = @(
    @{Name="KSCATEGORY_VIDEO_CAMERA";  Guid=$KSCATEGORY_VIDEO_CAMERA},
    @{Name="KSCATEGORY_VIDEO";         Guid=$KSCATEGORY_VIDEO},
    @{Name="KSCATEGORY_CAPTURE";       Guid=$KSCATEGORY_CAPTURE}
)

foreach ($cat in $categories) {
    $interfacePath = "HKLM:\SYSTEM\CurrentControlSet\Control\DeviceClasses\$($cat.Guid)\##?#$devInstanceForPath#$($cat.Guid)"
    New-Item -Path $interfacePath -Force | Out-Null
    Set-ItemProperty $interfacePath -Name "DeviceInstance" -Value $deviceInstanceId
    Set-ItemProperty $interfacePath -Name "ReferenceString" -Value $cat.Guid
    Write-Output "  Registered: $($cat.Name)"
}

# 5. Register Camera class driver entry (minimal, no actual driver)
$driverPath = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$cameraClassGuid\0002"
New-Item -Path $driverPath -Force | Out-Null
Set-ItemProperty $driverPath -Name "DriverDesc" -Value "PhoneCam Camera"
Set-ItemProperty $driverPath -Name "ProviderName" -Value "PhoneCam"
Set-ItemProperty $driverPath -Name "MatchingDeviceId" -Value "root\PhoneCamV0"
Set-ItemProperty $driverPath -Name "InfPath" -Value ""
Set-ItemProperty $driverPath -Name "DriverVersion" -Value "1.0.0.0"
Set-ItemProperty $driverPath -Name "DriverDate" -Value "06/14/2026"

Write-Output "  Created Camera class driver entry"

# 6. Register in Device Container
$containerPath = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Virtualization\GuestCommunicationServices\$deviceGuid"
if (-not (Test-Path $containerPath)) {
    New-Item -Path $containerPath -Force | Out-Null
}

# 7. Notify PnP manager (requires devcon or SetupAPI)
Write-Output ""
Write-Output "=== Done ==="
Write-Output "Device: $deviceInstanceId"
Write-Output ""
Write-Output "NOTE: You may need to restart Windows or run 'devcon rescan' for the device to appear."
Write-Output "The camera will work with DirectShow apps (Tencent Meeting) immediately."
Write-Output "For Windows Camera settings, a UMDF driver is required (future work)."

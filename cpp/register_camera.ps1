# register_camera.ps1 — Register PhoneCam as a Windows Camera device
# Must run as Administrator

$ErrorActionPreference = "Stop"

# Generate a unique container ID
$containerId = [guid]::NewGuid().ToString("B").ToUpper()

# Find next available device number under ROOT\Image
$basePath = "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT\Image"
$nextNum = 0
if (Test-Path $basePath) {
    $existing = Get-ChildItem $basePath -ErrorAction SilentlyContinue | 
        ForEach-Object { if ($_.PSChildName -match '^\d+$') { [int]$_.PSChildName } } |
        Sort-Object -Descending
    if ($existing) { $nextNum = $existing[0] + 1 }
}
$devId = "{0:D4}" -f $nextNum
$devPath = "$basePath\$devId"

Write-Output "Registering PhoneCam Camera as ROOT\Image\$devId"
Write-Output "ContainerID: $containerId"

# 1. Create device instance
New-Item -Path $devPath -Force | Out-Null
Set-ItemProperty $devPath -Name "ClassGUID" -Value "{ca3e7ab9-b4c3-4ae6-8251-579ef933890f}"
Set-ItemProperty $devPath -Name "Class" -Value "Camera"
Set-ItemProperty $devPath -Name "Mfg" -Value "PhoneCam"
Set-ItemProperty $devPath -Name "DeviceDesc" -Value "PhoneCam Camera"
Set-ItemProperty $devPath -Name "ContainerID" -Value $containerId
Set-ItemProperty $devPath -Name "Driver" -Value "{ca3e7ab9-b4c3-4ae6-8251-579ef933890f}\000$nextNum"
Set-ItemProperty $devPath -Name "Capabilities" -Value 0 -Type DWord
Set-ItemProperty $devPath -Name "Status" -Value 0x180200 -Type DWord  # DN_STARTED | DN_ENUMERABLE | DN_DRIVER_LOADED
Set-ItemProperty $devPath -Name "ConfigFlags" -Value 0 -Type DWord
Set-ItemProperty $devPath -Name "FriendlyName" -Value "PhoneCam Camera"
Set-ItemProperty $devPath -Name "Address" -Value "$nextNum" -Type DWord

# 2. Device Parameters — link to our COM CLSID
$devParams = "$devPath\Device Parameters"
New-Item -Path $devParams -Force | Out-Null

# CLSID for DirectShow filter
Set-ItemProperty $devParams -Name "CLSID" -Value "{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}"

# Video Capture Source proxy CLSID (standard)
Set-ItemProperty $devParams -Name "CustomCaptureSourceClsid" -Value "{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}"

# FriendlyName
Set-ItemProperty $devParams -Name "FriendlyName" -Value "PhoneCam Camera"

# 3. Register device interfaces
$categories = @{
    "KSCATEGORY_VIDEO_CAMERA"  = "{e5323777-f976-4f5b-9b55-b94699c46e44}"
    "KSCATEGORY_VIDEO"         = "{6994ad05-93ef-11d0-a3cc-00a0c9223196}"
    "KSCATEGORY_CAPTURE"       = "{65e8773d-8f56-11d0-a3b9-00a0c9223196}"
    "KSCATEGORY_REALTIME"      = "{EB897380-ABB0-11d0-B788-00A0C9223196}"
}

$deviceInstanceId = "ROOT\Image\$devId"

foreach ($cat in $categories.GetEnumerator()) {
    $interfacePath = "HKLM:\SYSTEM\CurrentControlSet\Control\DeviceClasses\$($cat.Value)\##?#$deviceInstanceId#$($cat.Value)"
    New-Item -Path $interfacePath -Force | Out-Null
    Set-ItemProperty $interfacePath -Name "DeviceInstance" -Value $deviceInstanceId
    Set-ItemProperty $interfacePath -Name "ReferenceString" -Value $cat.Value
    Write-Output "  Registered: $($cat.Key)"
}

# 4. Register Device Container
$containerPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\DeviceDescriptions\$containerId"
if (-not (Test-Path $containerPath)) {
    New-Item -Path $containerPath -Force | Out-Null
}
Set-ItemProperty $containerPath -Name "Label" -Value "PhoneCam Camera"
Set-ItemProperty $containerPath -Name "Icons" -Value @("{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}") -Type MultiString

# 5. Also register in DeviceSetup class
$setupPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\DeviceSetup\InstalledDevices\$containerId"
if (-not (Test-Path $setupPath)) {
    New-Item -Path $setupPath -Force | Out-Null
}
Set-ItemProperty $setupPath -Name "DeviceInstance" -Value $deviceInstanceId

Write-Output ""
Write-Output "=== Done ==="
Write-Output "Device registered as: $deviceInstanceId"
Write-Output "Restart Windows Camera app or Tencent Meeting to see the new camera"

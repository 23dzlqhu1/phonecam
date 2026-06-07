$env:HTTP_PROXY = "http://127.0.0.1:7897"
$env:HTTPS_PROXY = "http://127.0.0.1:7897"
$env:FLUTTER_STORAGE_BASE_URL = "https://storage.flutter-io.cn"
$env:PUB_HOSTED_URL = "https://pub.flutter-io.cn"

Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled False

cd "F:\PhoneCam\phone"
flutter build apk --debug 2>&1 | Out-File -FilePath "F:\PhoneCam\build_log.txt" -Encoding UTF8

Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled True

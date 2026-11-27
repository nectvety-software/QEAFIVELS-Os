$ErrorActionPreference='Continue'
Write-Output '=== KERNEL SOURCE TREES ==='
$roots=@('D:\QEAFIVELS-Os','D:\QeafivelsOfficial\QeafdropOs')
foreach($root in $roots) {
  Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match 'kernel|linux|common|source' } |
    Select-Object FullName
}
Write-Output '=== INITRAMFS SOURCE FILES ==='
Get-ChildItem 'D:\QEAFIVELS-Os','D:\QeafivelsOfficial\QeafdropOs' -Recurse -File -Filter initramfs.c -ErrorAction SilentlyContinue |
  Select-Object FullName,Length
Write-Output '=== KERNEL IMAGE SOURCES ==='
Get-ChildItem 'D:\QEAFIVELS-Os\build\violet','D:\QeafivelsOfficial\QeafdropOs\out' -Recurse -File -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -match 'Image|Image.gz|zImage|vmlinux|boot.img|boot\.img' } |
  Select-Object -First 40 FullName,Length

<#
Galaxy Tab S3 (SM-T820, SM-T825): installs Nura (postmarketOS), our Android
(a GSI on our open vendor), or both, from a Windows PC, and sets Nura up with
your account, Wi-Fi, time zone and keyboard. Needs only adb and fastboot
(Android platform tools: next to this script, in .\platform-tools, or in
PATH). setup.sh does the same on Linux.

The tablet does the work: this script starts our OrangeFox on it from RAM
(nothing is flashed for that), hands it the release's installer zip and
streams Nura's root and the GSI to it over USB.

usage (PowerShell):  .\setup.ps1 [-Gsi <GSI .img or .img.xz>] [-SshKey <public key file>] [<folder>]
  -SshKey: turn on Nura's SSH server with that key (key logins only);
  without it, setup asks whether to turn SSH on
  the folder (default: this script's): gts3-dualboot-*.zip,
  OrangeFox-mainline-<model>-fastboot-boot.img, and nura-root.img.xz (+ its
  .sha256) for Nura; a GSI (*system*.img(.xz) or *gsi*.img(.xz)) for Android
If Windows refuses to run scripts: powershell -ExecutionPolicy Bypass -File .\setup.ps1
The tablet: in lk2nd's fastboot mode (Volume Down at power-on, then
fastboot), or already in our OrangeFox.
ERASES the userdata partition (internal storage). The microSD card, the
tablet's firmware partitions and its recovery partition are not touched.
#>
param([string]$Folder = $PSScriptRoot, [string]$Gsi = "", [string]$SshKey = "")
$ErrorActionPreference = "Stop"
$T = "/tmp/gts3-setup"

function Say($s) { Write-Host $s }
function Step($s) { Write-Host ""; Write-Host "== $s" -ForegroundColor Cyan }
function Die($s) { Write-Host ""; Write-Host "error: $s" -ForegroundColor Red; exit 1 }
function Ask($prompt, $default) {
	$p = if ($default) { "$prompt [$default]" } else { $prompt }
	$a = Read-Host $p
	if ($a) { $a } else { $default }
}
function AskSecret($prompt) {
	# hidden at a console; answers piped in (tests) can't be read that way
	if ([Console]::IsInputRedirected) { return [Console]::In.ReadLine() }
	$s = Read-Host $prompt -AsSecureString
	$b = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($s)
	try { [Runtime.InteropServices.Marshal]::PtrToStringBSTR($b) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b) }
}

# adb and fastboot
function FindTool($name) {
	foreach ($d in @($PSScriptRoot, (Join-Path $PSScriptRoot "platform-tools"), $Folder, (Join-Path $Folder "platform-tools"))) {
		$p = Join-Path $d "$name.exe"
		if (Test-Path $p) { return $p }
	}
	$c = Get-Command $name -ErrorAction SilentlyContinue
	if ($c) { return $c.Source }
	Die "$name.exe not found: put Google's platform-tools next to this script (https://developer.android.com/tools/releases/platform-tools)"
}
$adb = FindTool "adb"
$fastboot = FindTool "fastboot"
# A native tool's output, stderr included, as text (fastboot prints to
# stderr); its status in $script:Rc. The argument line goes to the tool as
# it is: adb shell gets one quoted argument, run by the tablet's shell.
$script:Rc = 0
function Run($exe, $argline) {
	$psi = New-Object Diagnostics.ProcessStartInfo
	$psi.FileName = $exe
	$psi.Arguments = $argline
	$psi.UseShellExecute = $false
	# its own, empty stdin: adb shell would read the script's (the answers)
	$psi.RedirectStandardInput = $true
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$p = [Diagnostics.Process]::Start($psi)
	$p.StandardInput.Close()
	$o = $p.StandardOutput.ReadToEndAsync()
	$e = $p.StandardError.ReadToEndAsync()
	$p.WaitForExit()
	$script:Rc = $p.ExitCode
	return $o.Result + $e.Result
}

# The release files
$zip = Get-ChildItem -Path $Folder -Filter "gts3-dualboot-*.zip" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $zip) { Die "no gts3-dualboot-*.zip in $Folder" }
$root = Join-Path $Folder "nura-root.img.xz"
if (-not (Test-Path $root)) { $root = "" }
if (-not $Gsi) {
	$g = Get-ChildItem -Path $Folder -File -ErrorAction SilentlyContinue | Where-Object {
		($_.Name -match "system|gsi") -and ($_.Name -match "\.img(\.xz)?$") -and ($_.Name -notmatch "nura-root") } | Select-Object -First 1
	if ($g) { $Gsi = $g.FullName }
}
if ($Gsi -and -not (Test-Path $Gsi)) { Die "no such GSI: $Gsi" }
if (-not $root -and -not $Gsi) { Die "put nura-root.img.xz (Nura) and/or a GSI (Android) in $Folder" }

Step "What to install"
if ($root -and $Gsi) {
	Say "1) Nura only   2) Android only   3) both side by side"
	switch (Ask "Choose" "3") {
		"1" { $setup = "nura"; $Gsi = "" }
		"2" { $setup = "android"; $root = "" }
		"3" { $setup = "both" }
		default { Die "1, 2 or 3" }
	}
} elseif ($root) { $setup = "nura"; Say "Nura only (put a GSI in $Folder for Android too)" }
else { $setup = "android"; Say "Android only (no nura-root.img.xz in $Folder)" }
if ($root) { Say "Nura: $(Split-Path $root -Leaf)" }
if ($Gsi) { Say "Android: $(Split-Path $Gsi -Leaf)" }

$gsiFmt = ""
if ($Gsi) {
	if ($Gsi -match "\.xz$") { $gsiFmt = "xz" }
	elseif ($Gsi -match "\.img$") {
		$fs = [IO.File]::OpenRead($Gsi); $m = New-Object byte[] 4; [void]$fs.Read($m, 0, 4); $fs.Close()
		if ($m[0] -eq 0x3a -and $m[1] -eq 0xff -and $m[2] -eq 0x26 -and $m[3] -eq 0xed) {
			Die "$(Split-Path $Gsi -Leaf) is a sparse image: use the GSI's .img.xz, or a raw .img"
		}
		$gsiFmt = "raw"
	} else { Die "the GSI must be a .img or .img.xz (unzip a zip first)" }
}

# Nura's settings, checked here first (gts3-firstsetup checks them again)
$A = Join-Path ([IO.Path]::GetTempPath()) ("gts3-setup-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $A | Out-Null
$utf8 = New-Object Text.UTF8Encoding $false
function Put($name, $value) { [IO.File]::WriteAllText((Join-Path $A $name), $value, $utf8) }
try {
if ($root) {
	Step "Nura: your account and settings"
	while ($true) {
		$u = if ($env:USERNAME) { $env:USERNAME } elseif ($env:USER) { $env:USER } else { "user" }
		$v = Ask "User name (lowercase)" ($u.ToLower() -replace "[^a-z0-9_-]", "")
		if ($v -cmatch "^[a-z_][a-z0-9_-]{0,30}$") { break }
		Say "  lowercase letters, digits, - and _, starting with a letter"
	}
	Put "username" $v
	$v = Ask "Full name" ""
	if ($v) { Put "fullname" ($v -replace "[:,]", "") }
	while ($true) {
		$p1 = AskSecret "Password"; $p2 = AskSecret "Password again"
		if ($p1 -and $p1 -ceq $p2) { break }
		Say "  empty, or not the same; again"
	}
	Put "password" $p1
	$p1 = $null; $p2 = $null
	while ($true) {
		$v = Ask "Tablet name on the network" "tab-s3"
		if ($v -match "^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$") { break }
		Say "  letters, digits and -"
	}
	Put "hostname" $v
	# Windows time zone to IANA: .NET 6+ (PowerShell 7) knows it, else ask
	$tz = ""
	try {
		$iana = $null
		$id = [TimeZoneInfo]::Local.Id
		if ($id -match "^[A-Za-z_]+/[A-Za-z_/+-]+$") { $tz = $id }	# already IANA (not Windows)
		elseif ([TimeZoneInfo]::TryConvertWindowsIdToIanaId($id, [ref]$iana)) { $tz = $iana }
	} catch { }
	if (-not $tz) { Say "  Time zone as Region/City, e.g. Asia/Kolkata, Europe/Berlin, America/New_York" }
	Put "timezone" (Ask "Time zone" $(if ($tz) { $tz } else { "UTC" }))
	$c = Get-Culture
	$loc = ($c.Name -replace "-", "_") + ".UTF-8"
	if ($c.Name -notmatch "^[a-z]{2,3}-[A-Z]{2}$") { $loc = "en_US.UTF-8" }
	Put "locale" (Ask "Language (locale)" $loc)
	# the keyboard: English -> us (gb for en-GB), else mostly the country
	$kb = "us"
	try {
		$tag = (Get-WinUserLanguageList)[0].LanguageTag
		if ($tag -eq "en-GB") { $kb = "gb" }
		elseif ($tag -match "^en") { $kb = "us" }
		elseif ($tag -match "-([A-Z]{2})$") { $kb = $Matches[1].ToLower() }
	} catch { }
	Put "keyboard" (Ask "Keyboard layout(s), XKB names like us or de" $kb)
	$cc = "US"
	try { $cc = [Globalization.RegionInfo]::CurrentRegion.TwoLetterISORegionName } catch { }
	Put "country" ((Ask "Country, for Wi-Fi (two letters)" $cc).ToUpper())
	$v = Ask "Wi-Fi network to join (empty: none)" ""
	if ($v) {
		Put "wifi_ssid" $v
		while ($true) {
			$p1 = AskSecret "Wi-Fi password (empty: an open network)"
			if (-not $p1 -or ($p1.Length -ge 8 -and $p1.Length -le 63)) { break }
			Say "  8 to 63 characters"
		}
		if ($p1) { Put "wifi_psk" $p1 }
		$p1 = $null
	}
	# an encrypted folder (fscrypt on the eMMC's inline crypto engine)
	if ((Ask "Make an encrypted folder, ~/Private (unlocked with its own passphrase)? [y/N]" "") -match "^[yY]") {
		while ($true) {
			$p1 = AskSecret "Passphrase for ~/Private (8 characters or more)"
			$p2 = AskSecret "Passphrase again"
			if ($p1.Length -ge 8 -and $p1 -eq $p2) { break }
			Say "  shorter than 8, or not the same; again"
		}
		Put "private" $p1
		$p1 = $null; $p2 = $null
	}
	# SSH: log in from a PC (USB: 172.16.42.1, Wi-Fi: <tablet name>.local)
	if (-not $SshKey -and ((Ask "Turn on SSH (log in to the tablet from a PC)? [y/N]" "") -match "^[yY]")) {
		$k = ""
		foreach ($f in @("id_ed25519.pub", "id_ecdsa.pub", "id_rsa.pub")) {
			$c = Join-Path (Join-Path $HOME ".ssh") $f
			if (Test-Path $c) { $k = $c; break }
		}
		$SshKey = Ask "Your public key file (empty: log in with the password)" $k
		if (-not $SshKey) { Put "ssh" "yes" }
	}
	if ($SshKey) {
		if (-not (Test-Path $SshKey)) { Die "no such key file: $SshKey" }
		if ($SshKey -notmatch "\.pub$") { Die "${SshKey}: give the public key (.pub), not the private one" }
		$keys = (Get-Content $SshKey | Where-Object { $_ -match "^(ssh-|ecdsa-|sk-)" }) -join "`n"
		if (-not $keys) { Die "$SshKey has no public key line" }
		Put "ssh" "yes"
		Put "ssh_key" $keys
	}
}

# The tablet: in lk2nd (start our OrangeFox) or already in it
Step "The tablet"
function State {
	if ((Run $adb "devices") -match "\trecovery") { return "recovery" }
	if ((Run $fastboot "devices") -match "\S+\s+fastboot") { return "fastboot" }
	return "none"
}
Say "Connect it by USB, in lk2nd's fastboot mode (Volume Down at power-on, then"
Say "fastboot) or in our OrangeFox. Waiting..."
while ((State) -eq "none") { Start-Sleep 2 }
if ((State) -eq "fastboot") {
	$compat = Run $fastboot "getvar lk2nd:compatible"
	if ($compat -match "samsung,gts3lwifi") { $model = "gts3lwifi" }
	elseif ($compat -match "samsung,gts3llte") { $model = "gts3llte" }
	else { Die "not a Galaxy Tab S3 in lk2nd" }
	$fox = Join-Path $Folder "OrangeFox-mainline-$model-fastboot-boot.img"
	if (-not (Test-Path $fox)) { Die "no $(Split-Path $fox -Leaf) in $Folder" }
	Say "In lk2nd: starting our OrangeFox (from RAM)..."
	Run $fastboot "boot `"$fox`"" | Out-Null
	$n = 0
	while ((State) -ne "recovery") { Start-Sleep 2; $n++; if ($n -ge 60) { Die "OrangeFox didn't show up over adb" } }
}
# OrangeFox restarts USB once while it starts (MTP, which our kernel doesn't
# have): wait until its main page is up, then a little more
$n = 0
while ((Run $adb "shell `"grep -c Set.page:..main. /tmp/recovery.log`"") -notmatch "^[1-9]") {
	Start-Sleep 2; $n++; if ($n -ge 60) { Die "OrangeFox didn't finish starting" }
}
Start-Sleep 5
Run $adb "wait-for-recovery" | Out-Null
$compat = Run $adb "shell cat /proc/device-tree/compatible"
if ($compat -match "samsung,gts3lwifi") { Say "SM-T820 in OrangeFox" }
elseif ($compat -match "samsung,gts3llte") { Say "SM-T825 in OrangeFox" }
else { Die "the recovery isn't our OrangeFox on a Galaxy Tab S3" }

Step "Last check"
Say "This ERASES the tablet's internal storage (userdata): Android's apps and"
Say "data, a postmarketOS install. The microSD card is not touched."
Say "Installing: $setup"
if ((Read-Host "Type yes to go on") -ne "yes") { Die "stopped, nothing changed" }

$envs = "MODEL="
if ($env:GTS3_DATA_DEV) { $envs += " DATA_DEV=$($env:GTS3_DATA_DEV)" }
if ($env:GTS3_BOOT_DEV) { $envs += " BOOT_DEV=$($env:GTS3_BOOT_DEV)" }

# A stage of the installer on the tablet, with a file on its stdin or none;
# prints the installer's messages, returns adb's (the installer's) status
function Stage($stage, $vars, $inFile) {
	$psi = New-Object Diagnostics.ProcessStartInfo
	$psi.FileName = $adb
	$psi.Arguments = "shell -T `"cd $T && env $envs GTS3_STAGE=$stage $vars sh $T/ub 3 1 $T/installer.zip`""
	$psi.UseShellExecute = $false
	$psi.RedirectStandardInput = $true
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$p = [Diagnostics.Process]::Start($psi)
	$outTask = $p.StandardOutput.ReadToEndAsync()
	$errTask = $p.StandardError.ReadToEndAsync()
	if ($inFile) {
		$in = [IO.File]::OpenRead($inFile)
		$buf = New-Object byte[] (4MB)
		$total = $in.Length; $done = 0
		try {
			while (($r = $in.Read($buf, 0, $buf.Length)) -gt 0) {
				$p.StandardInput.BaseStream.Write($buf, 0, $r)
				$done += $r
				Write-Progress -Activity "Sending $(Split-Path $inFile -Leaf)" -PercentComplete ([int](100 * $done / $total))
			}
		} catch {
			Say "  the USB link dropped while sending"
		} finally { $in.Close() }
		Write-Progress -Activity "Sending" -Completed
	}
	try { $p.StandardInput.Close() } catch { }
	$p.WaitForExit()
	foreach ($l in ($outTask.Result -split "`r?`n")) {
		if ($l -match "^ui_print (.+)") { Say "  $($Matches[1])" }
	}
	if ($p.ExitCode -ne 0 -and $errTask.Result.Trim()) { Say "  $($errTask.Result.Trim())" }
	return $p.ExitCode
}
function Push($local, $remote) {
	Run $adb "push `"$local`" $remote" | Out-Null
	if ($script:Rc -eq 0) { return $true }
	Start-Sleep 3
	Run $adb "wait-for-recovery" | Out-Null
	Run $adb "push `"$local`" $remote" | Out-Null
	return ($script:Rc -eq 0)
}

Step "Copying the installer"
Run $adb "shell `"rm -rf $T && mkdir -p $T`"" | Out-Null
if (-not (Push $zip.FullName "$T/installer.zip")) { Die "adb push of the installer" }
Run $adb "shell `"cd $T && unzip -o -q installer.zip META-INF/com/google/android/update-binary && mv META-INF/com/google/android/update-binary ub`"" | Out-Null
if ($script:Rc -ne 0) { Die "can't unpack the installer on the tablet" }

Step "Layout"
if ((Stage "layout" "GTS3_SETUP=$setup" $null) -ne 0) { Die "the layout failed (see above)" }

function Sha($f) { (Get-FileHash -Algorithm SHA256 -Path $f).Hash.ToLower() }
function Send($part, $file, $fmt, $sum) {
	Step "Sending $(Split-Path $file -Leaf) ($([int]((Get-Item $file).Length / 1MB)) MiB)"
	return (Stage "recv" "GTS3_PART=$part GTS3_FORMAT=$fmt GTS3_SHA256=$sum" $file)
}
if ($root) {
	Say "Checking $(Split-Path $root -Leaf)..."
	$sum = Sha $root
	if (Test-Path "$root.sha256") {
		if ((Get-Content "$root.sha256" -Raw).Split(" ")[0].Trim() -ne $sum) { Die "$(Split-Path $root -Leaf) is damaged, download it again" }
	}
	if ((Send "nura" $root "xz" $sum) -ne 0) { Die "sending Nura's root failed: run setup again" }
}
if ($Gsi) {
	Say "Checking $(Split-Path $Gsi -Leaf)..."
	if ((Send "android_system" $Gsi $gsiFmt (Sha $Gsi)) -ne 0) { Die "sending the GSI failed: run setup again" }
}

Step "Finishing"
$fvars = ""
if ($root) {
	if (-not (Push $A "$T/answers")) { Die "adb push of the settings" }
	$fvars = "GTS3_ANSWERS=$T/answers GTS3_UTC=$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())"
}
$rc = Stage "finish" $fvars $null
Run $adb "shell `"rm -rf $T/answers`"" | Out-Null
if ($rc -ne 0) { Die "finishing failed (see above)" }

Step "Done"
$os = if ($root) { "Nura" } else { "Android" }
if ((Read-Host "Restart the tablet now? [y/N]") -match "^[yY]") {
	Run $adb "reboot" | Out-Null
	Say "It restarts into $os. Volume Down at power-on: lk2nd's menu."
} else {
	Say "Restart it when you like: it starts $os."
}
} finally {
	Remove-Item -Recurse -Force $A -ErrorAction SilentlyContinue
}

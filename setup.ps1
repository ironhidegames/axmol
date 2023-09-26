# the setup script of axmol, for powershell <= 5.1, 
# please execute command 'Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy Bypass -Force' in PowerShell Terminal
$myRoot = $PSScriptRoot
$AX_ROOT = $myRoot

$pwsh_ver = $PSVersionTable.PSVersion.ToString()

function mkdirs([string]$path) {
    if (!(Test-Path $path -PathType Container)) {
        if ([System.Version]$pwsh_ver -ge [System.Version]'5.0.0.0') {
            New-Item $path -ItemType Directory 1>$null
        }
        else {
            mkdir $path
        }
    }
}

if ([System.Version]$pwsh_ver -lt [System.Version]'5.0.0.0') {
    # try setup WMF5.1, require reboot, try run setup.ps1 several times
    Write-Host "Installing WMF5.1 ..."
    $osVer = [System.Environment]::OSVersion.Version
    
    if ($osVer.Major -ne 6) {
        throw "Unsupported OSVersion: $($osVer.ToString())"
    }
    if ($osVer.Minor -ne 1 -and $osVer -ne 3) {
        throw "Only win7 SP1 or win8 supported"
    }
    
    $is_win7 = $osVer.Minor -eq 1
    
    # [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 5.1 non-win10
    
    $prefix = Join-Path (Get-Location).Path 'tmp'
    
    mkdirs $prefix
    $curl = (New-Object Net.WebClient)
    
    # .net 4.5.2 prereq by WMF5.1
    $pkg_out = Join-Path $prefix 'NDP452-KB2901907-x86-x64-AllOS-ENU.exe'
    if (!(Test-Path $pkg_out -PathType Leaf)) {
        Write-Host "Downloading $pkg_out ..."
        $curl.DownloadFile('https://download.microsoft.com/download/E/2/1/E21644B5-2DF2-47C2-91BD-63C560427900/NDP452-KB2901907-x86-x64-AllOS-ENU.exe', $pkg_out)
        if (!$?) {
            del $pkg_out
        }
    }
    .\tmp\NDP452-KB2901907-x86-x64-AllOS-ENU.exe /q /norestart
    
    # WMF5.1: https://learn.microsoft.com/en-us/powershell/scripting/windows-powershell/wmf/setup/install-configure?view=powershell-7.3&source=recommendations#download-and-install-the-wmf-51-package
    if ($is_win7) {
        $wmf_pkg = 'Win7AndW2K8R2-KB3191566-x64.zip'
    }
    else {
        $wmf_pkg = 'Win8.1AndW2K12R2-KB3191564-x64.msu'
    }
    
    $pkg_out = Join-Path $prefix "$wmf_pkg"
    if (!(Test-Path $pkg_out -PathType Leaf)) {
        Write-Host "Downloading $pkg_out ..."
        $curl.DownloadFile("https://download.microsoft.com/download/6/F/5/6F5FF66C-6775-42B0-86C4-47D41F2DA187/$wmf_pkg", $pkg_out)
        if (!$?) {
            del $pkg_out
        }
    }
    if ($is_win7) {
        echo "Expanding $pkg_out to $prefix"
        function Expand-Zip($Path, $DestinationPath) {
            mkdirs $DestinationPath
            $shell = new-object -com shell.application
            $zip = $shell.NameSpace($Path)
            foreach ($item in $zip.items()) {
                $shell.Namespace($DestinationPath).copyhere($item)
            }
        }
        Expand-Zip -Path $pkg_out -DestinationPath $prefix\WMF51
        & "$prefix\WMF51\Install-WMF5.1.ps1"
    }
    else {
        wusa.exe $pkg_out /quiet /norestart
    }

    throw "PowerShell 5.0+ required, installed is: $pwsh_ver, after install WMF5.1 and restart computer, try again"
}

$build1kPath = Join-Path $myRoot '1k/build1k.ps1'
$prefix = Join-Path $myRoot 'tools/external'
if (!(Test-Path $prefix -PathType Container)) {
    mkdirs $prefix
}

# setup toolchains: glslcc, cmake, ninja, ndk, jdk, ...
. $build1kPath -setupOnly -prefix $prefix @args

$AX_CONSOLE_ROOT = Join-Path $AX_ROOT 'tools/console'

# https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_environment_variables
$IsWin = $IsWindows -or ("$env:OS" -eq 'Windows_NT')

if ($IsWin) {
    if ("$env:AX_ROOT" -ne "$AX_ROOT") {
        $env:AX_ROOT = $AX_ROOT
        [Environment]::SetEnvironmentVariable('AX_ROOT', $AX_ROOT, 'User')
    }

    $pathList = [System.Collections.ArrayList]$env:PATH.Split(';') # eval with system + user
    $isMeInPath = $pathList.IndexOf($AX_CONSOLE_ROOT) -ne -1
    $oldCmdRoot = $null
    $cmdInfo = Get-Command 'axmol' -ErrorAction SilentlyContinue
    if ($cmdInfo) {
        $cmdRootTmp = Split-Path $cmdInfo.Source -Parent
        if ($cmdRootTmp -ne $AX_CONSOLE_ROOT) {
            $oldCmdRoot = $cmdRootTmp
        }
    }
    
    if (!$isMeInPath -or $oldCmdRoot) {
        # Add console bin to User PATH
        $strPathList = [Environment]::GetEnvironmentVariable('PATH', 'User') # we need get real pathList from CurrentUser
        if ($strPathList) { 
            $pathList = [System.Collections.ArrayList]($strPathList.Split(';')) 
        }
        else { 
            $pathList = New-Object System.Collections.ArrayList 
        }
        
        if ($oldCmdRoot) {
            $pathList.Remove($oldCmdRoot)
        }
        
        if ($isMeInPath -and ($pathList[0] -ne $AX_CONSOLE_ROOT)) {
            $pathList.Remove($AX_CONSOLE_ROOT)
            $pathList.Insert(0, $AX_CONSOLE_ROOT)
        }
        
        $strPathList = $pathList -join ';'
        [Environment]::SetEnvironmentVariable('PATH', $strPathList, 'User')

        # Re-eval env:PATH to system + user
        $strPathListM = [Environment]::GetEnvironmentVariable('PATH', 'Machine')
        $env:PATH = "$strPathList;$strPathListM" # sync to PowerShell Terminal
    }
}
else {
    # update pwsh profile
    if (Test-Path $PROFILE -PathType Leaf) {
        $profileContent = Get-Content $PROFILE -raw
    }
    else {
        $profileContent = ''
    }

    $profileMods = 0
    $matchRet = [Regex]::Match($profileContent, "env\:AX_ROOT\s+\=\s+.*")
    if (!$matchRet.Success) {
        $profileContent += "# Add environment variable AX_ROOT for axmol`n"
        $profileContent += '$env:AX_ROOT = "{0}"{1}' -f $AX_ROOT, "`n"
        ++$profileMods
    }
    elseif ($env:AX_ROOT -ne $AX_ROOT) {
        # contains AX_ROOT statement, but not equal us
        Write-Host "Updating env AX_ROOT from ${env:AX_ROOT} to $AX_ROOT"
        $profileContent = [Regex]::Replace($profileContent, "env\:AX_ROOT\s+\=\s+.*", "env:AX_ROOT = '$AX_ROOT'")
        ++$profileMods
    }

    if ($profileContent.IndexOf('$env:PATH = ') -eq -1 -or !($axmolCmdInfo = (Get-Command axmol -ErrorAction SilentlyContinue)) -or $axmolCmdInfo.Source -ne "$AX_CONSOLE_ROOT/axmol") {
        $profileContent += "# Add axmol console tool to PATH`n"
        $profileContent += '$env:PATH = "${env:AX_ROOT}/tools/console:${env:PATH}"'
        $profileContent += "`n"
        ++$profileMods
    }

    $profileDir = Split-Path $PROFILE -Parent
    if (!(Test-Path $profileDir -PathType Container)) {
        mkdirs $profileDir
    }

    if ($profileMods) {
        Set-Content $PROFILE -Value $profileContent
    }

    # update ~/.bashrc, ~/.zshrc
    function updateUnixProfile($profileFile) {
        $profileMods = 0
        $profileContent = Get-Content $profileFile -raw
        $matchRet = [Regex]::Match($profileContent, "export AX_ROOT\=.*")
        if (!$matchRet.Success) {
            $profileContent += "# Add environment variable AX_ROOT for axmol`n"
            $profileContent += 'export AX_ROOT="{0}"{1}' -f $AX_ROOT, "`n"
            ++$profileMods
        }
        else {
            $stmtLine = 'export AX_ROOT="{0}"' -f $AX_ROOT
            if ($matchRet.Value -ne $stmtLine) {
                $profileContent = [Regex]::Replace($profileContent, "export AX_ROOT\=.*", $stmtLine)
            }
        }

        if ($profileContent.IndexOf('export PATH=$AX_ROOT/tools/console:')) {
            $profileContent += "# Add axmol console tool to PATH`n"
            $profileContent += 'export PATH=$AX_ROOT/tools/console:$PATH' -f "`n"
            ++$profileMods
        }
        if ($profileMods) {
            Set-Content $profileFile -Value $profileContent
        }
    }

    if (Test-Path ~/.bashrc -PathType Leaf) {
        updateUnixProfile ~/.bashrc
    }
    
    if (Test-Path ~/.zshrc -PathType Leaf) {
        updateUnixProfile ~/.zshrc
    }

    # update macos launchctl
    if ($IsMacOS) {
        # for GUI app, android studio can find AX_ROOT
        launchctl setenv AX_ROOT $env:AX_ROOT
    }
}


if ($IsLinux) {
    Write-Host "Are you continue install linux dependencies for axmol? (y/n) " -NoNewline
    $answer = Read-Host
    if ($answer -like 'y*') {
        if ($(Get-Command 'dpkg' -ErrorAction SilentlyContinue)) {
            $b1k.println("It will take few minutes")
            sudo apt update
            # for vm, libxxf86vm-dev also required

            $DEPENDS = @()

            $DEPENDS += 'libx11-dev'
            $DEPENDS += 'automake'
            $DEPENDS += 'libtool'
            $DEPENDS += 'cmake'
            $DEPENDS += 'libxmu-dev'
            $DEPENDS += 'libglu1-mesa-dev'
            $DEPENDS += 'libgl2ps-dev'
            $DEPENDS += 'libxi-dev'
            $DEPENDS += 'libzip-dev'
            $DEPENDS += 'libpng-dev'
            $DEPENDS += 'libfontconfig1-dev'
            $DEPENDS += 'libgtk-3-dev'
            $DEPENDS += 'binutils'
            # $DEPENDS += 'libbsd-dev'
            $DEPENDS += 'libasound2-dev'
            $DEPENDS += 'libxxf86vm-dev'
            $DEPENDS += 'libvlc-dev', 'libvlccore-dev', 'vlc'

            # if vlc encouter codec error, install
            # sudo apt install ubuntu-restricted-extras

            sudo apt install --allow-unauthenticated --yes $DEPENDS > /dev/null
        }
        elseif ($(Get-Command 'pacman' -ErrorAction SilentlyContinue)) {
            $DEPENDS = @(
                'git',
                'cmake',
                'make',
                'libx11', 
                'libxrandr',
                'libxinerama',
                'libxcursor',
                'libxi',
                'fontconfig',
                'gtk3',
                'vlc'
            )
            sudo pacman -S --needed --noconfirm @DEPENDS
        }
        else {
            $b1k.println("Warning: current Linux distro isn't officially supported by axmol community")
        }

        b1k_print "Installing axmol freetype into linux system directory ..."
        Set-Location "$AX_ROOT/thirdparty/freetype"
        cmake -B build '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_INSTALL_PREFIX=/usr' '-DDISABLE_FORCE_DEBUG_POSTFIX=ON' '-DFT_DISABLE_HARFBUZZ=ON' '-DFT_DISABLE_BROTLI=ON' '-DFT_DISABLE_BZIP2=ON' '-DBUILD_SHARED_LIBS=ON'
        sudo cmake --build build --config Release --target install
        Set-Location -
    }
}

$b1k.pause("setup successfully, please restart the terminal to make added system variables take effect")

# HDLGameInstaller

The HDLoader game installer

## Introduction

The HDLoader game installer allows the user to install PlayStation 2 games onto the installed Harddisk Drive, for direct booting with the HDDOSD (Browser v2.00 update).

It can also be used as an alternative to HDLDump, as this software does not use the HDLDump protocol and hence entirely uses TCP for data transfer. This means that unlike HDLDump, it's more reliable.

It has the following features:

- Installs all PlayStation 2 games from the CD/DVD drive.
- Installs all PlayStation 2 games remotely from a PC, over a network.
- Installed games can be booted directly from the HDDOSD.
- Allows the user to manage games locally on the PlayStation 2 console itself.
- Allows the user to manage games remotely from a PC, over a network.
- Network performance uses the latest ethernet modules from the PS2SDK, giving about 4MB/s.
- Allows the user to specify the savedata icon to use with the game.
- Supports games >4GB and DVD-DL games.
- Game list can be sorted alphabetically (controlled from the PlayStation 2).
- Supports the Dynamic Host Configuration Protocol (DHCP), for easy set up.

## Setting Storage

Settings are now saved into the HDLGameInstaller save data folder:

    hdd0:__common/Your Saves/HDLGAMEINSTALLER

If you wish to delete the save, you may do so with the HDD Browser or compatible homebrew (e.g. wLaunchELF).

## Notes on network support

- HDLGameInstaller now maintains its own network configuration file on the HDD unit.
- If you have not configured HDLGameInstaller before, your existing homebrew network settings will be automatically imported from IPCONFIG.DAT.
- Network settings can now be configured from the Options menu.
- Please ensure that TCP ports 45061 and 45062 are allowed in your network.
- If you use the Windows Firewall, you may have to allow "public" access for HDLGManClient.exe.
- Pad support worsens performance. However, it is possible to connect to the PS2 at any time, even when the options screen is displayed.  If this is done, then the software will not disable pad input until the options menu is closed. For best performance, keep the options menu closed.

## Supported Devices

HDLGameInstaller may be installed (copied) onto and can be booted from:

- USB Mass Storage Devices. 
  - Only USB disks are supported. Multi-function devices are not supported.
  - The disk must have only one partition. Otherwise, the first partition will be accessed.
- PlayStation 2 HDD Unit.

**Other devices are not supported.**

## Known limitations/bugs

- The icon preview does not totally right. I don't have the knowledge nor resources to commit towards fixing it up. Someone else will have to solve this, sorry.
- The UI isn't the nicest one, but it works.
- No Japanese input support on the PS2 installer's end.
- DVD9 games are supported from the PC client only, as the CDVDMAN module within the boot ROMs of all consoles does not support DVD9 layer 1.
- Not compatible with the APAEXT partitioning scheme (i.e. ToxicOS). **!!!Do not use this software with a disk formatted with APAEXT, or data loss will occur!!!**
- As the variable-width font may result in an uneven number of characters being displayed on the screen as a line is scrolled, the user may observe that the cursor might jump back by one place while scrolling. This is not a bug.

### Changelog for v0.821

- Disabled pad input when a PC client is connected, to improve performance.
- Network status will be displayed when a PC client is connected, every 10s.
- NEW 2018/12/09 - Fixed the problem with the soft keyboard not waiting for the circle/cross button to be released before considering the user's intended input.

### Changelog for the PC client

- Increased data connection timeout to 80s, to allow the server's connection attempts to be exhausted before the PC client indicates an error.
- Added a prompt to delete the game if it was already installed, when the user adds a game to install.

## Supported languages

For more information on supported languages, click here. A template for translating this software is provided in the downloads section of this page.

Supported languages and their translation status:

- Japanese - Unassigned.
- French - Translated by DaSA.
- Spanish - Translated by ElPatas.
- German - Translated by Delta_force.
- Italian - Translated by master991.
- Dutch - Translated by an anonymous person.
- Portuguese - Translated by GillBert.

## What is HDLGManClient?

HDLGameInstaller allows the user to install and manage PlayStation 2 games. This can be done either with the PlayStation 2 console itself or remotely over the network with a PC. HDLGManClient is the PC client that allows the user to connect to HDLGameInstaller, when it's left running on the user's PlayStation 2 console.

## What is HDLGameUpdater?

The HDLoader game updater automatically updates the bootstrap program of all your HDLoader games (the program which makes these games bootable from the HDD OSD/browser updater v2.00, aka Mini OPL/Diskload).

This software will not automatically make games which are not already bootable, bootable. The current version is meant to be used for updating your games to have the same bootstrap program as the one installed by latest version of HDLGameInstaller.

# A/B Booting Setup

The scripts in this directory can be used to setup measurement probes that can
be updated using [Raspberry Pi's A/B booting implementation](https://www.raspberrypi.com/documentation/computers/config_txt.html#autoboot-txt).
`patch.sh` can be used to write a new probe image to an SD card that consists of
six partitions: The first partition contains a single file that determines
from which partitions to boot from in normal and tryboot mode. Partitions two
and three are used as the boot partitions (mounted under `/boot/firmware`) for
the A and B images respectively. The fourth partition is an extended partition
and necessary to accommodate more than 4 partitions. The last two partitions
contain the root file systems of the A and B image.

## Update mechanism

Once a probe is set up to use A/B booting, updating works as follows:

```console
# probe-update -f <update-archive>
```

Running this with an (xz compressed) tar archive containing the files
`boot.img` and `root.img` copies the file contents into the currently inactive
boot and root partitions, copies some configuration files to the new root
partition and does some sanity checks.

The probe can then be rebooted into the newly written partition by using the
RPi's tryboot mechanism:

```console
# reboot "0 tryboot"
```

Finally, to persist the update either manually invoke the update script
(`probe-update --switch-autoboot`) or use the provided systemd units to automatically
persist the update an hour after booting. The update is persisted by changing
the config file in the first partition to specify the newly written partition as
the default boot partition and the old partition as the new tryboot partition.

If booting the new image fails the RPi should automatically reboot into the old partition.

## Remotely upgrading probes to the A/B booting setup

We wanted to switch our already deployed probes to the aforementioned A/B
booting setup to alleviate some of the risk associated with upgrading probes for
future updates. Unfortunately, that meant reflashing the SD cards of our
existing probes. To do this remotely, we came up with a solution that employed a
customized initramfs (which runs from a memory-backed filesystem), allowing us
to change the layout of the SD card.

### Creating a custom initramfs

We started by installing Raspberry Pi OS on a development probe and updating
its initramfs using the already included
[initramfs-tools](https://manpages.debian.org/buster/initramfs-tools-core/initramfs-tools.7.en.html)
package. Using configuration hook scripts, we added some additional tools and
configuration: [Dropbear](https://matt.ucc.asn.au/dropbear/dropbear.html) (a
small SSH server), [WireGuard](https://www.wireguard.com/) (probes are only
reachable over a WireGuard VPN in our setup), Rsync, and some
partitioning/filesystem tools. We then added boot scripts configuring the
WireGuard interface, starting the SSH server, and delaying the rest of the boot
process to provide enough time to connect to the initramfs. We also made sure
to enable the Linux kernel watchdog so that probes would reboot with the old
configuration in case we were unable to connect.

Finally, we updated the `cmdline.txt` file to set the `ip=dhcp` and `panic=60`
parameters, `ip` being a kernel command-line option to set up networking
during boot and `panic=60` making the initramfs init script reboot on errors
(after 60 seconds) instead of dropping into a debug shell.

### Update process

To update one of the deployed probes, we used the following steps.

#### Preparation

First, we made sure that the probe had the same firmware version as our
development probe by using the `rpi-eeprom-update` tool to update the EEPROM and
by copying all `start*.elf` and `fixup*.dat` files from the development probe
boot partition. For the custom initramfs, we copied the following files from our
dev probe to a new directory in the boot partition: `*.dtb`, `kernel*.img`,
`initramfs*`, `cmdline.txt`, and the `overlays` directory. The only change we
made was to update the `root=PARTUUID=` argument with the PARTUUID of the probe's
root partition.

Finally, we copied the `config.txt` file from the development probe's boot
partition to a file named `tryboot.txt` in the updatee probe's boot partition
and added the line `os_prefix=new/`, pointing to the new directory containing the
custom initramfs. With all of this setup done, the probe can now be rebooted
into the new custom initramfs by running `sudo reboot "0 tryboot"`.

#### Updating probes from the initramfs

After connecting to the initramfs, we switched the `config.txt` and `tryboot.txt`
files, making sure that the probe would always boot into the custom initramfs.
At this point, we changed the partitioning scheme and copied the new boot and
root partition images to the probe before testing the new image by rebooting
from the new boot partition (`sudo reboot <boot partition>`). To finish the
upgrade, we added the following file named `autoboot.txt` to the first partition,
enabling the A/B booting mechanism:

```
[all]
tryboot_a_b=1
boot_partition=2
[tryboot]
boot_partition=3
```

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

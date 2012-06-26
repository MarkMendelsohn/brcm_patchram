brcm-patchram
=============

This work is based on:

	https://github.com/MarkMendelsohn/BlueZ-support-for-Broadcom

The intention of the fork is to:

* Clean up code -- Compiling with warnings enabled yields quite a few warnings -- some of which point to potential errors.
* Consolidate functionality into one utility -- Each of the updating methods do the same thing over differnt mediums (serial, usb, etc.) so there is a lot of overlap and common patterns across each of the utiities.
* Use libbluetooth instead of direct writes where possible.
* Package for debian, ubuntu, and redhat.

Purpose
=======

The purpose of these utilities is to read a file that contains a series of HCI layer bluetooth commands and send them to a bluetooth device.


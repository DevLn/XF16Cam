#! /bin/bash

conf_file=.config

echo "*"
echo "* XRADIO SDK Configuration"
echo "*"

# chip selection
choice_ok=0
echo "Chip"
echo "  1. XR872"
echo "  2. XR808CT"
echo "  3. XR808ST"
echo -n "choice[1-3]: "

read choice

if [[ $choice == 1 ]]; then
	echo "__CONFIG_CHIP_TYPE ?= xr872" > $conf_file
	choice_ok=1
fi

if [[ $choice == 2 ]]; then
	echo "__CONFIG_CHIP_TYPE ?= xr808" > $conf_file
	echo "__CONFIG_XR808_SUB_TYPE ?= ct" >> $conf_file
	choice_ok=1
fi

if [[ $choice == 3 ]]; then
	echo "__CONFIG_CHIP_TYPE ?= xr808" > $conf_file
	echo "__CONFIG_XR808_SUB_TYPE ?= st" >> $conf_file
	choice_ok=1
fi

if [[ $choice_ok == 0 ]]; then
	echo "ERROR: Invalid choice!"
	exit
fi

# HOSC selection
choice_ok=0
echo ""
echo "External high speed crystal oscillator"
echo "  1. 24M"
echo "  2. 26M"
echo "  3. 40M"
echo -n "choice[1-3]: "

read choice

if [[ $choice == 1 ]]; then
	echo "__CONFIG_HOSC_TYPE ?= 24" >> $conf_file
	choice_ok=1
fi

if [[ $choice == 2 ]]; then
	echo "__CONFIG_HOSC_TYPE ?= 26" >> $conf_file
	choice_ok=1
fi

if [[ $choice == 3 ]]; then
	echo "__CONFIG_HOSC_TYPE ?= 40" >> $conf_file
	choice_ok=1
fi

if [[ $choice_ok == 0 ]]; then
	echo "ERROR: Invalid choice!"
	rm $conf_file
	exit
fi

exit

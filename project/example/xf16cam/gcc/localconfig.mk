#
# project local config options, override the global config options
#

# ----------------------------------------------------------------------------
# override global config options
# ----------------------------------------------------------------------------
# enable/disable wlan, default to y
export __CONFIG_WLAN := y
export __CONFIG_WLAN_STA := y
export __CONFIG_WLAN_AP := y
export __CONFIG_WLAN_MONITOR := n

# Keep a verified, compressed update in the second half of the 1 MiB flash.
export __CONFIG_OTA := y
export __CONFIG_OTA_POLICY := 0x01

# enable/disable XIP, default to y
export __CONFIG_XIP := y

# enable/disable JPEG, default to n
export __CONFIG_JPEG := y
export __CONFIG_JPEG_SHARE_64K := n

export __CONFIG_PSRAM := n
export __CONFIG_PSRAM_CHIP_OPI32 := n

# GCC 8 diagnoses warnings in legacy SDK code that are outside this app.
export WARNINGS_AS_ERRORS := n

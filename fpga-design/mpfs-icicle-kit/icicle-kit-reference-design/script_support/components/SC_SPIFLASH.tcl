# Creating SmartDesign "SC_SPIFLASH"
set sd_name {SC_SPIFLASH}
create_smartdesign -sd_name ${sd_name}

# Disable auto promotion of pins of type 'pad'
auto_promote_pad_pins -promote_all 0

# Create top level Scalar Ports
sd_create_scalar_port -sd_name ${sd_name} -port_name {APB_bif_PENABLE} -port_direction {IN}
sd_create_scalar_port -sd_name ${sd_name} -port_name {APB_bif_PSEL} -port_direction {IN}
sd_create_scalar_port -sd_name ${sd_name} -port_name {APB_bif_PWRITE} -port_direction {IN}
sd_create_scalar_port -sd_name ${sd_name} -port_name {DI} -port_direction {IN} -port_is_pad {1}
sd_create_scalar_port -sd_name ${sd_name} -port_name {FLASH} -port_direction {IN} -port_is_pad {1}
sd_create_scalar_port -sd_name ${sd_name} -port_name {IFACE} -port_direction {IN} -port_is_pad {1}
sd_create_scalar_port -sd_name ${sd_name} -port_name {PCLK} -port_direction {IN}
sd_create_scalar_port -sd_name ${sd_name} -port_name {PRESETN} -port_direction {IN}

sd_create_scalar_port -sd_name ${sd_name} -port_name {APB_bif_PREADY} -port_direction {OUT}
sd_create_scalar_port -sd_name ${sd_name} -port_name {APB_bif_PSLVERR} -port_direction {OUT}
sd_create_scalar_port -sd_name ${sd_name} -port_name {DO} -port_direction {OUT} -port_is_pad {1}

sd_create_scalar_port -sd_name ${sd_name} -port_name {CLK} -port_direction {INOUT} -port_is_pad {1}
sd_create_scalar_port -sd_name ${sd_name} -port_name {SS} -port_direction {INOUT} -port_is_pad {1}

# Create top level Bus Ports
sd_create_bus_port -sd_name ${sd_name} -port_name {APB_bif_PADDR} -port_direction {IN} -port_range {[6:0]}
sd_create_bus_port -sd_name ${sd_name} -port_name {APB_bif_PWDATA} -port_direction {IN} -port_range {[31:0]}

sd_create_bus_port -sd_name ${sd_name} -port_name {APB_bif_PRDATA} -port_direction {OUT} -port_range {[31:0]}


# Create top level Bus interface Ports
sd_create_bif_port -sd_name ${sd_name} -port_name {APB_bif} -port_bif_vlnv {AMBA:AMBA2:APB:r0p0} -port_bif_role {slave} -port_bif_mapping {\
"PADDR:APB_bif_PADDR" \
"PSELx:APB_bif_PSEL" \
"PENABLE:APB_bif_PENABLE" \
"PWRITE:APB_bif_PWRITE" \
"PRDATA:APB_bif_PRDATA" \
"PWDATA:APB_bif_PWDATA" \
"PREADY:APB_bif_PREADY" \
"PSLVERR:APB_bif_PSLVERR" } 

# Add CORESPI_C0_0 instance
sd_instantiate_component -sd_name ${sd_name} -component_name {CORESPI_C0} -instance_name {CORESPI_C0_0}
sd_create_pin_slices -sd_name ${sd_name} -pin_name {CORESPI_C0_0:SPISS} -pin_slices {[0:0]}
sd_create_pin_slices -sd_name ${sd_name} -pin_name {CORESPI_C0_0:SPISS} -pin_slices {[1:1]}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPISS[1:1]}
sd_create_pin_slices -sd_name ${sd_name} -pin_name {CORESPI_C0_0:SPISS} -pin_slices {[2:2]}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPISS[2:2]}
sd_create_pin_slices -sd_name ${sd_name} -pin_name {CORESPI_C0_0:SPISS} -pin_slices {[3:3]}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPISS[3:3]}
sd_create_pin_slices -sd_name ${sd_name} -pin_name {CORESPI_C0_0:SPISS} -pin_slices {[4:4]}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPISS[4:4]}
sd_create_pin_slices -sd_name ${sd_name} -pin_name {CORESPI_C0_0:SPISS} -pin_slices {[5:5]}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPISS[5:5]}
sd_create_pin_slices -sd_name ${sd_name} -pin_name {CORESPI_C0_0:SPISS} -pin_slices {[6:6]}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPISS[6:6]}
sd_create_pin_slices -sd_name ${sd_name} -pin_name {CORESPI_C0_0:SPISS} -pin_slices {[7:7]}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPISS[7:7]}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPIINT}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPIRXAVAIL}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPITXRFM}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {CORESPI_C0_0:SPIMODE}



# Add PF_SPI_0 instance
sd_instantiate_macro -sd_name ${sd_name} -macro_name {PF_SPI} -instance_name {PF_SPI_0}
sd_mark_pins_unused -sd_name ${sd_name} -pin_names {PF_SPI_0:FAB_SPI_OWNER}
sd_connect_pins_to_constant -sd_name ${sd_name} -pin_names {PF_SPI_0:CLK_OE} -value {VCC}
sd_connect_pins_to_constant -sd_name ${sd_name} -pin_names {PF_SPI_0:SS_OE} -value {VCC}



# Add scalar net connections
sd_connect_pins -sd_name ${sd_name} -pin_names {"CLK" "PF_SPI_0:CLK" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"CORESPI_C0_0:PCLK" "PCLK" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"CORESPI_C0_0:PRESETN" "PRESETN" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"CORESPI_C0_0:SPICLKI" "PF_SPI_0:CLK_I" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"CORESPI_C0_0:SPIOEN" "PF_SPI_0:D_OE" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"CORESPI_C0_0:SPISCLKO" "PF_SPI_0:CLK_O" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"CORESPI_C0_0:SPISDI" "PF_SPI_0:D_I" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"CORESPI_C0_0:SPISDO" "PF_SPI_0:D_O" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"CORESPI_C0_0:SPISSI" "PF_SPI_0:SS_I" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"CORESPI_C0_0:SPISS[0:0]" "PF_SPI_0:SS_O" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"DI" "PF_SPI_0:DI" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"DO" "PF_SPI_0:DO" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"FLASH" "PF_SPI_0:FLASH" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"IFACE" "PF_SPI_0:IFACE" }
sd_connect_pins -sd_name ${sd_name} -pin_names {"PF_SPI_0:SS" "SS" }


# Add bus interface net connections
sd_connect_pins -sd_name ${sd_name} -pin_names {"APB_bif" "CORESPI_C0_0:APB_bif" }

# Re-enable auto promotion of pins of type 'pad'
auto_promote_pad_pins -promote_all 1
# Save the SmartDesign 
save_smartdesign -sd_name ${sd_name}
# Generate SmartDesign "SC_SPIFLASH"
generate_component -component_name ${sd_name}

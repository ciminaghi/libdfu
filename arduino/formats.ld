/* formats.ld linker script for the arduino version of the library */
PHDRS
{
  dport0_0_phdr PT_LOAD;
  dram0_0_phdr PT_LOAD;
  dram0_0_bss_phdr PT_LOAD;
  iram1_0_phdr PT_LOAD;
  irom0_0_phdr PT_LOAD;
}

SECTIONS
{
        .my_tables : ALIGN(4)
        {
                *(SORT(.binary-formats*))
        } >dram0_0_seg :dram0_0_phdr
};


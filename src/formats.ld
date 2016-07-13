/* link binary formats */
SECTIONS
{
	.data : {
	      registered_formats_start = .;
	      *(.binary-formats);
	      registered_formats_end = .;
	}
}

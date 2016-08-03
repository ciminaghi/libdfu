/* link rx methods */
SECTIONS
{
	.data : {
	      registered_rx_methods_start = .;
	      *(.rx-methods);
	      registered_rx_methods_end = .;
	}
}

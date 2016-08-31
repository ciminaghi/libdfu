/* link binary formats */
SECTIONS
{
	.data : {
	      http_urls_start = .;
	      *(.http-urls);
	      http_urls_end = .;
	}
}

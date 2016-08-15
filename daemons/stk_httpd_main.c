
extern int stk_httpd_main(int shared, int argc, char *argv[]);

int main(int argc, char *argv[])
{
	return stk_httpd_main(0,argc,argv);
}


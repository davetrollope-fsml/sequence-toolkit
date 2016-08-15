extern int stk_named_main(int shared, int argc, char *argv[]);

int main(int argc, char *argv[])
{
	return stk_named_main(0,argc,argv);
}


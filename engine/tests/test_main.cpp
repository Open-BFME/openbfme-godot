// OpenBFME unit tests (doctest). GPL-3.0.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "Common/PathIO.h"

#include <fstream>

static int testMain(int argc, char **argv)
{
	doctest::Context context(argc, argv);
	context.setAsDefaultForAssertsOutOfTestCases();
	const auto &options = *doctest::getContextOptions();
	std::ofstream output;
	// Doctest's default --out handling opens a narrow path, which uses the Windows ACP.
	if (!options.quiet && options.out.size())
	{
		output.open(PathIO::native(options.out.c_str()));
		if (!output)
		{
			std::fprintf(stderr, "cannot open test output: %s\n", options.out.c_str());
			return 1;
		}
		context.setCout(&output);
	}
	return context.run();
}

int main(int argc, char **argv)
{
	return PathIO::runMain(argc, argv, testMain);
}

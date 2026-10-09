// OpenBFME unit tests. GPL-3.0.

#include "RetailTestMount.h"

#include "Common/RetailArchivePolicy.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#ifndef OPENBFME_TEST_DATA_DIR
#define OPENBFME_TEST_DATA_DIR "tests/data"
#endif

namespace retailtest
{

namespace
{

std::string envOrEmpty(const char *name)
{
	const char *v = std::getenv(name);
	return v ? std::string(v) : std::string();
}

std::unique_ptr<Mount> build(const std::vector<std::pair<std::string, std::string>> &installsWanted)
{
	std::unique_ptr<Mount> m = std::make_unique<Mount>();
	std::vector<RetailInstall> installs;
	for (const auto &want : installsWanted)
	{
		RetailInstall inst;
		inst.label = want.first;
		inst.root = want.second;
		std::string policyId = (want.first == "rotwk") ? "rotwk-201" : "bfme2-106";
		if (!RetailArchivePolicy::loadBuiltin(policyId, inst.policy, &m->error))
		{
			return m;
		}
		installs.push_back(inst);
	}
	m->fs = std::make_unique<Win32BIGFileSystem>();
	RetailMountOptions options; // md5 verification is the smoke test's job; the size/name policy still runs
	RetailMountReport report = MountRetailArchives(*m->fs, installs, options);
	if (!report.ok)
	{
		for (const std::string &e : report.errors)
		{
			m->error += e + "\n";
		}
		m->fs.reset();
		return m;
	}
	for (const MountedArchive &a : report.mounted)
	{
		m->archives.push_back(a.install + ":" + a.canonicalPath);
	}
	return m;
}

} // namespace

Mount *pureMount()
{
	static std::unique_ptr<Mount> mount;
	static bool tried = false;
	std::string rotwk = envOrEmpty("ROTWK_INSTALL");
	std::string bfme2 = envOrEmpty("BFME2_INSTALL");
	if (rotwk.empty() || bfme2.empty())
	{
		return nullptr;
	}
	if (!tried)
	{
		tried = true;
		mount = build({ { "rotwk", rotwk }, { "bfme2", bfme2 } });
	}
	return mount.get();
}

Mount *bfme2Mount()
{
	static std::unique_ptr<Mount> mount;
	static bool tried = false;
	std::string bfme2 = envOrEmpty("BFME2_INSTALL");
	if (bfme2.empty())
	{
		return nullptr;
	}
	if (!tried)
	{
		tried = true;
		mount = build({ { "bfme2", bfme2 } });
	}
	return mount.get();
}

std::string bfme2Install()
{
	return envOrEmpty("BFME2_INSTALL");
}

std::string dataDir()
{
	return OPENBFME_TEST_DATA_DIR;
}

bool readLocalFile(const std::string &path, std::vector<unsigned char> &out, std::string *error)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		if (error)
		{
			*error = "cannot open " + path;
		}
		return false;
	}
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

void printSkip(const char *testName)
{
	std::printf("SKIP: %s needs ROTWK_INSTALL and BFME2_INSTALL (retail files); not run\n", testName);
	std::fflush(stdout);
}

} // namespace retailtest

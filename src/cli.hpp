#pragma once

#include <list>
#include <nlohmann/json.hpp>
#include <string>

#include "argparse/argparse.hpp"

// Thin wrapper that owns the root parser and all sub-parsers by value
// (argparse stores subparser references, so they must stay alive together).
struct Cli {
	argparse::ArgumentParser            root;
	std::list<argparse::ArgumentParser> subs;

	Cli(const std::string &name, const std::string &version)
	    : root(name, version)
	{}

	argparse::ArgumentParser &add_sub(const std::string &name)
	{
		auto &p = subs.emplace_back(name);
		root.add_subparser(p);
		return p;
	}
};

// Registers every subcommand and its arguments onto `cli`.
void build_cli(Cli &cli);

// Applies whichever subcommand was actually used on the command line
// by merging its values into `cfg`.
void apply_subcommands(Cli &cli, nlohmann::json &cfg);

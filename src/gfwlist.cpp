#include <fstream>

#include <algorithm>

#include <regex>
#include <cctype>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include "gfwlist.hpp"

namespace logging = boost::log;
namespace keywords = boost::log::keywords;
void replace_string(std::string &orig, const std::string &src, const std::string &dest)
{
	// auto pos = 0;
	for (auto pos = orig.find(src); pos != std::string::npos; pos = orig.find(src, pos)) {
		orig.replace(pos, src.size(), dest);
		pos += dest.size();
	}
}
void gfw_list::load_list(const std::string &file)//, const std::string &white, const std::string &block)
{
	if (file == "") {
		return;
	}
 	std::string line;
// 	for (std::ifstream in_wh(white); std::getline(in_wh, line); ) {
// #if USE_VECTOR
// 		_whites.push_back(line);
// #else
// 		_whites.insert(line);
// #endif
// 	}
// 	for (std::ifstream in_bl(block); std::getline(in_bl, line); ) {
// #if USE_VECTOR
// 		_blocks.push_back(line);
// #else
// 		_blocks.insert(line);
// #endif
// 	}

	std::ifstream infile(file);
	std::getline(infile, line);
	for (; std::getline(infile, line); ) {
		if (line[0] == '!'
		    || (line[0] == '@' && line[1] == '@')
		    || isspace(line[0]) ) {
			continue;
		}
		replace_string(line, "|", "");
		auto pos  = line.find("//", 0);
		if ( pos != std::string::npos) {
			line = line.substr(pos+2);
		}
		pos = line.find("/", 0);
		if ( pos != std::string::npos) {
			line = line.substr(0,pos);
		}
		replace_string(line, ".", "\\.");
		replace_string(line, "*", ".*");
		if (line.empty()) {
			continue;
		}
		line.append("$");
//		_hosts.insert(line);
		_hosts.push_back(line);
	}
	auto last = std::unique(_hosts.begin(), _hosts.end());
	_hosts.erase(last, _hosts.end());
}
int gfw_list::add_host(const std::string & host)
{
//	_hosts.insert(host);
	_hosts.push_back(host);
	return 0;
}
bool gfw_list::is_blocked(const std::string & host) const
{
	if (_hosts.size() == 0)
		return true;
//	return _hosts.end() == _hosts.find(host);
//#if USE_VECTOR
// 	if (_whites.end() != std::find(_whites.begin(), _whites.end(), host)) {
// //	if (_whites.end() != _whites.find(host) ) {
// 		BOOST_LOG_TRIVIAL(info) << " find  "<<host << " in whitelist";
// 		return false;
// 	}
// 	if (_blocks.end() != std::find(_blocks.begin(), _blocks.end(), host)) {
// 		//if (_blocks.end() != _blocks.find(host)) {
// 		BOOST_LOG_TRIVIAL(info) << " find  "<<host << " in blocklist";
// 		return true;
// 	}
	// bool res = _hosts.end() != std::find(_hosts.begin(), _hosts.end(), host);
	// if (res) {
	// 	_blocks.push_back(host);
	// } else {
	// 	_whites.push_back(host);
	// }
	// return res;
	for (const auto & i : _hosts) {
		std::regex entry(i);
		if (std::regex_search(host, entry)) {
			BOOST_LOG_TRIVIAL(info) << " find reg "<<i << " match" << host;
// #if USE_VECTOR
// 			_blocks.push_back(host);
// #else
// 			_blocks.insert(host);
// #endif
			return true;
		}
	}
	// #if USE_VECTOR
	// _whites.push_back(host);
	// #else
	// _whites.insert(host);
	// #endif
	return false;
}

// int main(int argc, char *argv[])
// {
// 	gfw_list mylist;
// 	BOOST_LOG_TRIVIAL(info) << " start load list";
// 	mylist.load_list("rawlist", "wlist", "blist");
// 	BOOST_LOG_TRIVIAL(info) << " end load list";
// 	if (argc > 1) {
// 		BOOST_LOG_TRIVIAL(info) << " find "<< argv[1] << ":" << mylist.is_blocked(argv[1]);
// 	}
// 	mylist.save_list("newlist", "wlist", "blist");
// 	BOOST_LOG_TRIVIAL(info) << " end save list";
// 	return 0;
// }

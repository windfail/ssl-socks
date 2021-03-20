#ifndef _GFW_LIST_HPP
#define _GFW_LIST_HPP

#include <string>
#include <vector>
#include <unordered_set>

#define USE_VECTOR 1
class gfw_list
{
public:
	gfw_list(const std::string &file)
		{
			load_list(file);
		}
	bool is_blocked(const std::string &host);

	int add_host(const std::string &host);
//	void save_list(const std::string &file, const std::string & white, const std::string & block);
private:
	void load_list(const std::string &file);//, const std::string & white, const std::string & block);
//	std::unordered_set<std::string> _hosts;
	std::vector<std::string> _hosts;
#if USE_VECTOR
	std::vector<std::string> _whites;
	std::vector<std::string> _blocks;
#else
	std::unordered_set<std::string> _whites;
	std::unordered_set<std::string> _blocks;
#endif
};

#endif

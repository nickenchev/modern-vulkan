#include "utils.h"
#include <fstream>
#include <sstream>

std::string readTextFile(const std::string &filePath)
{
	std::ifstream infile(filePath);
	if (infile.is_open())
	{
		std::stringstream buffer;
		buffer << infile.rdbuf();
		const std::string output = buffer.str();
		infile.close();
		return output;
	}
	return std::string();
}

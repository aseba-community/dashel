#include <dashel/dashel.h>
#include <iostream>

using namespace Dashel;

int main()
{
	std::map<int, std::pair<std::string, std::string> > ports = SerialPortEnumerator::getPorts();

	for (std::map<int, std::pair<std::string, std::string> >::iterator it = ports.begin(); it != ports.end(); ++it)
	{
		std::cout << "(" << it->first << ") " << it->second.first << " [" << it->second.second << "]" << std::endl;
	}


	return 0;
}

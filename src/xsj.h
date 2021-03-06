#include "xsj-trans.h"
#include <string>
#include <map>

//##############################################################################
//##############################################################################
enum XSJProcssType {
	XPT_Query,
	XPT_Execute,
	XPT_Lock,
	XPT_Unsolicited,
};

//##############################################################################
//##############################################################################
struct XSJCallData {
	HSERVICE 		hService;
	DWORD 			dwCommand;
	XSJProcssType 	dwType;
	DWORD 			dwTimeout;
	LPVOID 			lpData;
};

//##############################################################################
//##############################################################################
inline Translator* findTranslator(const std::string cmd, bool toXfs) {
	static int size = 0;
	static std::map<std::string, Translator*> translators;

	if(size == 0) {
		Translator* pTranslator = GetTranslators(size);
		for(int i=0; i<size; i++) {
			if(pTranslator->fpToXFS) {
				translators[std::string("2XFS") + pTranslator->strCommand] = pTranslator;
			}

			if(pTranslator->fpToJS) {
				translators[std::string("2JS") + pTranslator->strCommand] = pTranslator;
			}
			pTranslator++;
		}
	}

	std::string id = toXfs?"2XFS":"2JS";
	id += cmd;

	auto translator = translators.find(id);
	if(translator == translators.end()) {
		return nullptr;
	}

	return translator->second;
}

//##############################################################################
//##############################################################################
inline bool JS2XFS(const json& j, XSJCallData& cd) {
	if (j.find("service") == j.end() || j.find("command") == j.end() ||
		j.find("timeOut") == j.end() ||	j.find("data") == j.end()) {
		return false;
	}

	if(!j["service"].is_number() || !j["command"].is_string() ||
		!j["timeOut"].is_number() || !j["data"].is_object()) {
		return false;
	}

	std::string strCommand = j["command"];
	cd.hService = j["service"];
	cd.dwTimeout = j["timeOut"];
	cd.dwType = (strCommand.find("WFS_INF_") == 0)?XPT_Query:XPT_Execute;

	if(cd.dwType == XPT_Query) {
		cd.dwCommand = GetXfsInfoCmdId(strCommand);
	}
	else {
		cd.dwCommand = GetXfsExecuteCmdId(strCommand);
	}

	auto pTranslator = findTranslator(strCommand, true);
	if(pTranslator) {
		cd.lpData = pTranslator->fpToXFS(j["data"]);
	}
	else{
		if(j.find("data") != j.end() && !j["data"].empty()) {
			CWAR << "Translator to XFS not found for: " << strCommand << ": " << j.dump();
		}
		cd.lpData = nullptr;
	}

	return true;
}

//##############################################################################
//##############################################################################
inline std::string XFS2JS(XSJProcssType pt, const LPWFSRESULT result, json& j) {
	std::string strCommand;

	if(pt == XPT_Query) {
		strCommand = GetXfsInfoCmdName(result->u.dwCommandCode);
	}
	else if(pt == XPT_Execute) {
		strCommand = GetXfsExecuteCmdName(result->u.dwCommandCode);
	}
	else if (pt == XPT_Unsolicited) {
		strCommand = GetXfsEventName(result->u.dwEventID);
	}
	else {
		j["result"] = XSJ_ListNullTerminatedValues<HSERVICE, HSERVICE>((HSERVICE*)result->lpBuffer, nullptr);
		strCommand = "ServiceClosed";
		j["id"] = strCommand;
		return strCommand;
	}

	j = XSJTranslate<WFSRESULT>(result);
	j["id"] = strCommand;

	auto pTranslator = findTranslator(strCommand, false);
	if(pTranslator) {
		j["data"] = pTranslator->fpToJS(result->lpBuffer);
		j["name"] = pTranslator->strCodeName;
		strCommand = pTranslator->strCodeName;
	}
	else if(result->lpBuffer){
		CWAR << "Translator to JS not found for: " << strCommand;
	}

	return strCommand;
}
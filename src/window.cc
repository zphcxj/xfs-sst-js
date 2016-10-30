#include "Window.h"
#include <uv.h>
#include "xsj-trans.h"
#include "xfsapi.h"
#include "common.h"
#include "json.hpp"
#include "xfsdevice.h"

using json = nlohmann::json;

//#############################################################################
//#############################################################################
#define DefineXFSProcessor(evt, proc) void Window::proc(LPWFSRESULT pData)

//#############################################################################
//#############################################################################
Nan::Persistent<v8::Function> Window::constructor;
Window* Window::m_lpInstance = nullptr;
DWORD Window::m_nodeThread = GetCurrentThreadId();

//#############################################################################
//#############################################################################
Window::Window(const Nan::FunctionCallbackInfo<v8::Value>& info):
	m_this(v8::Isolate::GetCurrent(), info.Holder()), m_sarted(false), 
	m_hwnd(nullptr), m_traceLevel(WFS_TRACE_API), m_timeOut(10000), m_loop(nullptr),
	m_xfsStarted(false) {
}

//#############################################################################
//#############################################################################
Window::~Window() {
}

//#############################################################################
//#############################################################################
void Window::Init(v8::Local<v8::Object> exports) {
	Nan::HandleScope scope;

	// Prepare constructor template
	v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
	tpl->SetClassName(Nan::New("XfsMgr").ToLocalChecked());
	tpl->InstanceTemplate()->SetInternalFieldCount(1);

	// Prototype
	Nan::SetPrototypeMethod(tpl, "__call", Call);

	constructor.Reset(tpl->GetFunction());
	exports->Set(Nan::New("XfsMgr").ToLocalChecked(), tpl->GetFunction());
}

//#############################################################################
//#############################################################################
bool Window::IsNodeThread() {
	return Window::m_nodeThread == GetCurrentThreadId();
}

//#############################################################################
//#############################################################################
void Window::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
	if (info.IsConstructCall()) {
		assert(nullptr == m_lpInstance);
		m_lpInstance = new Window(info);
		m_lpInstance->Wrap(info.This());
		info.GetReturnValue().Set(info.This());
	}
	else {
		v8::Local<v8::Function> cons = Nan::New<v8::Function>(constructor);
		info.GetReturnValue().Set(cons->NewInstance(0, nullptr));
	}
}

//#############################################################################
//#############################################################################
bool Window::StartThread() {
	if(!m_sarted) {
		PostNodeEvent("win.start", "");
		m_sarted = true;

		m_loop = uv_default_loop();
		uv_async_init(m_loop, &m_async, OnNodeMessage);
		uv_thread_create(&m_thread_id, WinMessagePump, this);
		m_async.data = new MessageQueue();

		return m_sarted;
	}

	Nan::ThrowError("XFS Manager started.");;
	return false;
}

//#############################################################################
//#############################################################################
void Window::OnNodeMessage(uv_async_t *handle) {
	InterThreadMessage* pMessage = nullptr;
	(*(MessageQueue*)handle->data) >> pMessage;
	while(nullptr != pMessage) {
		ProcessNodeMessage(pMessage);

		if (pMessage->strTitle == "win.exit") {
			((Window*)pMessage->lpData)->m_sarted = false;
			uv_close((uv_handle_t*)handle, nullptr);
			delete handle->data;
			handle->data = nullptr;
			break;
		}

		pMessage = nullptr;
		(*(MessageQueue*)handle->data) >> pMessage;
	}

}

void Window::ProcessNodeMessage(InterThreadMessage* pMessage)
{
	v8::HandleScope scope(v8::Isolate::GetCurrent());
	v8::Local<v8::Value> argv[2] = {
		Nan::New(pMessage->strTitle).ToLocalChecked(),
		Nan::New(pMessage->strData).ToLocalChecked(),
	};
	
	if (HSERVICE_MGR == pMessage->hService) {

		if (pMessage->strTitle.find("console.") != -1) {
			OutputDebugString(pMessage->strTitle.c_str());
			((Window*)pMessage->lpData)->SendNodeEvent(pMessage->strTitle, pMessage->strData);
		}
		else {
			((Window*)pMessage->lpData)->PostNodeEvent(pMessage->strTitle, pMessage->strData);
		}
	}
}

//#############################################################################
//#############################################################################
void Window::WinMessagePump(LPVOID p) {
	Window* pThis = (Window*)p;
	const char CLASS_NAME[] = "XSJ Window Class";
	WNDCLASS wc = {};

	wc.lpfnWndProc = WindowProc;
	wc.hInstance = nullptr; // hInstance;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);
	HWND hwnd = CreateWindowEx(
		0,                              // Optional window styles.
		CLASS_NAME,                     // Window class
		"Sample Window",    			// Window text
		WS_OVERLAPPEDWINDOW,            // Window style
										// Size and position
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,       // Parent window    
		NULL,       // Menu
		NULL, //hInstance,  // Instance handle
		NULL        // Additional application data
	);

	if (hwnd == NULL) {
		CASS(hwnd != NULL) << "Can't create Window: 0x" << std::hex << GetLastError();
		return;
	}

	pThis->m_hwnd = hwnd;
	ShowWindow(hwnd, SW_HIDE);

	// Run the message loop.
	CINF << "Start Windows Messaging Pump";
	MSG msg = {};

	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	pThis->SendToNode(HSERVICE_MGR, "win.exit", "", pThis);
	return;
}

//#############################################################################
//#############################################################################
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	LPWFSRESULT pData = (LPWFSRESULT)lParam;
	XfsDevice* pDevice = nullptr;
	if (pData && Window::m_lpInstance) {
		auto it = Window::m_lpInstance->m_services.find(pData->hService);
		if (it != Window::m_lpInstance->m_services.end()) {
			pDevice = it->second;
		}
	}

	switch (uMsg) {
	case WM_CREATE:
		if (Window::m_lpInstance) {
			Window::m_lpInstance->SendToNode(HSERVICE_MGR, "initialize", "", Window::m_lpInstance);
		}
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_PAINT:	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);

		FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

		EndPaint(hwnd, &ps);
	}

	case WM_NODE2WIN:	{
		auto pmsg = (InterThreadMessage*)wParam;
		if (pmsg) {
			CINF << pmsg->strTitle << ":" << pmsg->strData;
		}
		if(pmsg && pmsg->hService == HSERVICE_MGR && 
			pmsg->lpData == Window::m_lpInstance && Window::m_lpInstance != nullptr) {
			Window::m_lpInstance->ProcessV8Message(pmsg);
		}

		delete pmsg;
		break;
	}

	case WM_NODE2WINDEV: {
		auto pmsg = (InterThreadMessage*)wParam;
		if (pmsg) {
			CINF << pmsg->strTitle << ":" << pmsg->strData;
		}
		if (pmsg && pmsg->hService != HSERVICE_MGR &&
			pmsg->lpData != nullptr) {
			XfsDevice* pDevice = (XfsDevice*)pmsg->lpData;
			pDevice->ProcessV8Message(pmsg->strTitle, pmsg->strData);
		}

		delete pmsg;
		break;
	}

	XFSDeviceProcessorEntry(WFS_CLOSE_COMPLETE, CloseComplete);
	XFSDeviceProcessorEntry(WFS_LOCK_COMPLETE, LockComplete);
	XFSDeviceProcessorEntry(WFS_UNLOCK_COMPLETE, UnlockComplete);
	XFSDeviceProcessorEntry(WFS_REGISTER_COMPLETE, RegisterComplete);
	XFSDeviceProcessorEntry(WFS_DEREGISTER_COMPLETE, DeregisterComplete);
	XFSDeviceProcessorEntry(WFS_GETINFO_COMPLETE, GetInfoComplete);
	XFSDeviceProcessorEntry(WFS_EXECUTE_COMPLETE, ExecuteComplete);

	XFSProcessorEntry(WFS_OPEN_COMPLETE, OpenComplete);
	XFSProcessorEntry(WFS_EXECUTE_EVENT, ExecuteEvent);
	XFSProcessorEntry(WFS_SERVICE_EVENT, ServiceEvent);
	XFSProcessorEntry(WFS_USER_EVENT, UserEvent);
	XFSProcessorEntry(WFS_SYSTEM_EVENT, SystemEvent);
	XFSProcessorEntry(WFS_TIMER_EVENT, TimerEvent);
	return 0;

	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

//#############################################################################
//#############################################################################
void Window::Call(const Nan::FunctionCallbackInfo<v8::Value>& info) {
	if (info.Length() != 2) {
		Nan::ThrowSyntaxError("Wrong argument number.");;
		return;
	}

	if (!info[0]->IsString() || !info[1]->IsString()) {
		Nan::ThrowTypeError("Wrong argument type.");;
		return;
	}

	std::string title(*Nan::Utf8String(info[0]));
	std::string data(*Nan::Utf8String(info[1]));

	CINF << "Call " << title << " " << data;

	Window* obj = ObjectWrap::Unwrap<Window>(info.Holder());
	auto ret = obj->Command(title, data);
	info.GetReturnValue().Set(ret);
}

//#############################################################################
//#############################################################################
v8::Local<v8::Value> Window::Command(const std::string & title, const std::string & data) {
	if ("initialize" == title) {
		return Nan::New(StartThread());
	}

	if (!m_sarted) {
		Nan::ThrowError("XFS Manager not started.");;
		return Nan::Null();
	}

	//getTraceLevel
	if ("getTraceLevel" == title) {
		return Nan::New(GetXfsTraceLevelName(m_traceLevel)).ToLocalChecked();
	}

	//setTraceLevel
	if ("setTraceLevel" == title) {
		m_traceLevel = GetXfsTraceLevelId(data);
		return Nan::New(GetXfsTraceLevelName(m_traceLevel)).ToLocalChecked();
	}

	//getTimeOut
	if ("getTimeOut" == title) {
		return Nan::New((int)m_timeOut);
	}

	//setTimeOut
	if ("setTimeOut" == title) {
		m_timeOut = atol(data.c_str());
		return Nan::New((int)m_timeOut);
	}

	if (!m_hwnd) {
		Nan::ThrowError("XFS Manager is starting...");;
		return Nan::Null();
	}

	// start
	// cleanUp
	// open, 
	// uninitialize

	auto pmsg = new InterThreadMessage({ HSERVICE_MGR, title, data, this});
	if (!PostMessage(m_hwnd, WM_NODE2WIN, (WPARAM)pmsg, NULL)) {
		delete pmsg;
		return Nan::New(false);
	}
	
	return Nan::New(true);
}

//#############################################################################
//#############################################################################
void Window::ProcessV8Message(InterThreadMessage* pmsg) {
	if (!pmsg) {
		return;
	}

	json j = json::parse(pmsg->strData);

	// open
	if (pmsg->strTitle == "open") {
		if (!m_xfsStarted) {
			SendToNode(HSERVICE_MGR, "open.error", "xfs not sarted", this);
			return;
		}

		DWORD dwVersionsRequired = j["versionsRequired"];
		std::string logicalName = j["logicalName"];
		HAPP appHandle = (HAPP)((int)j["appHandle"]);
		std::string appId = j["appId"];
		DWORD traceLevel = GetXfsTraceLevelId(j["traceLevel"]);
		DWORD timeOut = j["timeOut"];

		WFSVERSION srvcVersion;
		WFSVERSION spiVersion;
		HSERVICE   hService;
		REQUESTID requestID;

		HRESULT hr = WFSAsyncOpen(const_cast<char *>(logicalName.c_str()), appHandle,
			(appId=="_no_used")?NULL:const_cast<char *>(appId.c_str()), traceLevel, timeOut,
			&hService, m_hwnd, dwVersionsRequired, &srvcVersion, &spiVersion, &requestID);

		if (WFS_SUCCESS != hr) {
			SendToNode(HSERVICE_MGR, "open.error", GetXfsErrorCodeName(hr), this);
			return;
		}

		json jr;
		j["service"] = (int)hService;
		j["serviceVersion"] = XSJTranslate(&srvcVersion);
		j["spiVersion"] = XSJTranslate(&spiVersion);
		j["requestID"] = requestID;
		j["class"] = XFSLSKey(logicalName, "class");

		SendToNode(HSERVICE_MGR, "open", j.dump(), this);
		return;
	}

	// start
	if (pmsg->strTitle == "start") {
		if (m_xfsStarted) {
			SendToNode(HSERVICE_MGR, "start.error", "xfs sarted", this);
			return;
		}
		DWORD dwVersionsRequired = j["versionRequired"];
		WFSVERSION version = { 0 };
		HRESULT hr = WFSStartUp(dwVersionsRequired, &version);
		if (WFS_SUCCESS != hr) {
			SendToNode(HSERVICE_MGR, "start.error", GetXfsErrorCodeName(hr), this);
			return;
		}

		m_xfsStarted = true;

		auto j = XSJTranslate(&version);
		SendToNode(HSERVICE_MGR, "start", j.dump(), this);
		return;
	}

	// cleanUp
	if (pmsg->strTitle == "cleanUp") {
		if (!m_xfsStarted) {
			SendToNode(HSERVICE_MGR, "cleanUp.error", "xfs not sarted", this);
			return;
		}

		HRESULT hr = WFSCleanUp();
		if (WFS_SUCCESS != hr) {
			SendToNode(HSERVICE_MGR, "cleanUp.error", GetXfsErrorCodeName(hr), this);
			return;
		}

		m_xfsStarted = false;
		SendToNode(HSERVICE_MGR, "cleanUp", "", this);
	}

	// uninitialize
	if (pmsg->strTitle == "uninitialize") {
		if (m_xfsStarted) {
			SendToNode(HSERVICE_MGR, "uninitialize.error", "xfs is running.", this);
			return;
		}

		PostMessage(((Window*)pmsg->lpData)->m_hwnd, WM_CLOSE, NULL, NULL);
		SendToNode(HSERVICE_MGR, "uninitialize", "", this);
	}

	// createAppHandle
	if ("createAppHandle" == pmsg->strTitle) {
		HAPP handle;
		HRESULT hr = WFSCreateAppHandle(&handle);

		if (WFS_SUCCESS != hr) {
			SendToNode(HSERVICE_MGR, "createAppHandle.error", GetXfsErrorCodeName(hr), this);
			return;
		}

		auto j = XSJTranslate(handle);
		SendToNode(HSERVICE_MGR, "createAppHandle", j.dump(), this);
		return;
	}

	// destroyAppHandle
	if ("destroyAppHandle" == pmsg->strTitle) {
		HAPP handle = (HAPP) atol(pmsg->strData.c_str());
		HRESULT hr = WFSDestroyAppHandle(handle);

		if (WFS_SUCCESS != hr) {
			SendToNode(HSERVICE_MGR, "destroyAppHandle.error", GetXfsErrorCodeName(hr), this);
			return;
		}

		SendToNode(HSERVICE_MGR, "destroyAppHandle", "", this);
		return;
	}
}

//#############################################################################
//#############################################################################
void Window::SendToNode(HSERVICE hService, const std::string& title, const std::string& data, LPVOID lpData) {
	auto pReturn = new InterThreadMessage({ hService,title, data, lpData });
	(*(MessageQueue*)m_async.data) << pReturn;
	uv_async_send(&m_async);
}

//#############################################################################
//#############################################################################
v8::Local<v8::Value> Window::PostNodeEvent(const std::string & title, const std::string & data) {
	v8::Local<v8::Value> argv[2] = {
		Nan::New(title).ToLocalChecked(),
		Nan::New(data).ToLocalChecked(),
	};

	return Nan::MakeCallback(Nan::New(m_this), "_post", 2, argv);
}

//#############################################################################
//#############################################################################
v8::Local<v8::Value> Window::SendNodeEvent(const std::string & title, const std::string & data) {
	v8::Local<v8::Value> argv[2] = {
		Nan::New(title).ToLocalChecked(),
		Nan::New(data).ToLocalChecked(),
	};

	return Nan::MakeCallback(Nan::New(m_this), "_send", 2, argv);
}

//#############################################################################
//#############################################################################
DefineXFSProcessor(WFS_OPEN_COMPLETE, OpenComplete) {
	//pData
	json j = XSJTranslate(pData);
	SendToNode(HSERVICE_MGR, "open.complete", j.dump(), this);
}

//#############################################################################
//#############################################################################
DefineXFSProcessor(WFS_EXECUTE_EVENT, ExecuteEvent) {

}

//#############################################################################
//#############################################################################
DefineXFSProcessor(WFS_SERVICE_EVENT, ServiceEvent) {

}

//#############################################################################
//#############################################################################
DefineXFSProcessor(WFS_USER_EVENT, UserEvent) {

}

//#############################################################################
//#############################################################################
DefineXFSProcessor(WFS_SYSTEM_EVENT, SystemEvent) {

}

//#############################################################################
//#############################################################################
DefineXFSProcessor(WFS_TIMER_EVENT, TimerEvent) {

}
#include "oneshot.h"

/******************
 * HERE BE DRAGONS
 ******************/

#include "eventthread.h"
#include "debugwriter.h"
#include "bitmap.h"
#include "font.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

// OS-Specific code
#if defined _WIN32
	#define OS_W32
	#define WIN32_LEAN_AND_MEAN
	#define SECURITY_WIN32
	#include <windows.h>
	#include <mmsystem.h>
	#include <security.h>
	#include <shlobj.h>
	#include <SDL2/SDL_syswm.h>
#elif defined __APPLE__ || __linux__
	#include <stdlib.h>
	#include <unistd.h>
	#include <pwd.h>
	#include <dlfcn.h>

	#ifdef __APPLE__
		#define OS_OSX
		#include <dispatch/dispatch.h>
	#else
		#define OS_LINUX
		#include <gtk/gtk.h>
		#include <gdk/gdk.h>
		#include "xdg-user-dir-lookup.h"
	#endif
#else
	#error "Operating system not detected."
#endif

#define DEF_SCREEN_W 640
#define DEF_SCREEN_H 480

struct OneshotPrivate
{
	// Main SDL window
	SDL_Window *window;

	// String data
	std::string os;
	std::string lang;
	std::string userName;
	std::string savePath;
	std::string docsPath;
	std::string gamePath;
	std::string journal;

	// Dialog text
	std::string txtYes;
	std::string txtNo;

	// Booleans
	bool exiting;
	bool allowExit;

	OneshotPrivate()
		: window(0)
	{
	}

	~OneshotPrivate()
	{
	}
};

//OS-SPECIFIC FUNCTIONS
#if defined OS_LINUX
struct linux_DialogData
{
	// Input
	int type;
	const char *body;
	const char *title;

	// Output
	bool result;
};

static int linux_dialog(void *rawData)
{
	linux_DialogData *data = reinterpret_cast<linux_DialogData*>(rawData);

	// Determine correct flags
	GtkMessageType gtktype;
	GtkButtonsType gtkbuttons = GTK_BUTTONS_OK;
	switch (data->type)
	{
		case Oneshot::MSG_INFO:
			gtktype = GTK_MESSAGE_INFO;
			break;
		case Oneshot::MSG_YESNO:
			gtktype = GTK_MESSAGE_QUESTION;
			gtkbuttons = GTK_BUTTONS_YES_NO;
			break;
		case Oneshot::MSG_WARN:
			gtktype = GTK_MESSAGE_WARNING;
			break;
		case Oneshot::MSG_ERR:
			gtktype = GTK_MESSAGE_ERROR;
			break;
		default:
			gtk_main_quit();
			return 0;
	}

	// Display dialog and get result
	GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, gtktype, gtkbuttons, "%s", data->body);
	gtk_window_set_title(GTK_WINDOW(dialog), data->title);
	int result = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	// Interpret result and return
	data->result = (result == GTK_RESPONSE_OK || result == GTK_RESPONSE_YES);
	gtk_main_quit();
	return 0;
}
#elif defined OS_W32
/* Convert WCHAR pointer to std::string */
static std::string w32_fromWide(const WCHAR *ustr)
{
	std::string result;
	int size = WideCharToMultiByte(CP_UTF8, 0, ustr, -1, 0, 0, 0, 0);
	if (size > 0)
	{
		CHAR *str = new CHAR[size];
		if (WideCharToMultiByte(CP_UTF8, 0, ustr, -1, str, size, 0, 0) == size)
			result = str;
		delete [] str;
	}
	return result;
}
/* Convert WCHAR pointer from const char* */
/* (unused)
static WCHAR *w32_toWide(const char *str)
{
	if (str)
	{
		int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, 0, 0);
		if (size > 0)
		{
			WCHAR *ustr = new WCHAR[size];
			if (MultiByteToWideChar(CP_UTF8, 0, str, -1, ustr, size) == size)
				return ustr;
			delete [] ustr;
		}
	}

	//Return empty string
	WCHAR *ustr = new WCHAR[1];
	*ustr = 0;
	return ustr;
}
*/
#endif

Oneshot::Oneshot(RGSSThreadData &threadData) :
    threadData(threadData)
{
	p = new OneshotPrivate();
	p->window = threadData.window;
	p->savePath = threadData.config.customDataPath.substr(0, threadData.config.customDataPath.size() - 1);
	p->allowExit = true;
	p->exiting = false;
	#ifdef OS_W32
		p->os = "windows";
	#elif defined OS_OSX
		p->os = "macos";
	#else
		p->os = "linux";
	#endif

	/********************
	 * USERNAME/DOCS PATH
	 ********************/
#if defined OS_W32
	//Get language code
	WCHAR wlang[9];
	GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, wlang, sizeof(wlang) / sizeof(WCHAR));
	p->lang = w32_fromWide(wlang) + "_";
	GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, wlang, sizeof(wlang) / sizeof(WCHAR));
	p->lang += w32_fromWide(wlang);

	//Get user's name
	ULONG size = 0;
	GetUserNameExW(NameDisplay, 0, &size);
	if (GetLastError() == ERROR_MORE_DATA)
	{
		//Get their full (display) name
		wchar_t* name = new wchar_t[size];
		GetUserNameExW(NameDisplay, name, &size);
		p->userName = w32_fromWide(name);
		delete [] name;
	}
	else
	{
		//Get their login name
		DWORD size2 = 0;
		GetUserNameW(0, &size2);
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			wchar_t* name = new wchar_t[size2];
			GetUserNameW(name, &size2);
			p->userName = w32_fromWide(name);
			delete [] name;
		}
	}

	// Get documents path
	//char* path = new char[MAX_PATH];
	//SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, path);
	//p->docsPath = w32_fromWide((WCHAR*) path);
	p->docsPath = getenv("USERPROFILE") + std::string("\\Documents");
	p->gamePath = p->docsPath + "\\My Games";
	p->journal = "_______.exe";
#else
	// Get language code
	const char *lc_all = getenv("LC_ALL");
	const char *lang = getenv("LANG");
	const char *code = (lc_all ? lc_all : lang);
	if (code)
	{
		// find first dot, copy language code
		int end = 0;
		for (; code[end] && code[end] != '.'; ++end) {}
		p->lang = std::string(code, end);
	}
	else
		p->lang = "en";

	// Get user's name
	#ifdef OS_OSX
		struct passwd *pwd = getpwuid(geteuid());
	#elif defined OS_LINUX
		struct passwd *pwd = getpwuid(getuid());
	#endif
	if (pwd)
	{
		if (pwd->pw_gecos && pwd->pw_gecos[0] && pwd->pw_gecos[0] != ',')
		{
			//Get the user's full name
			int comma = 0;
			for (; pwd->pw_gecos[comma] && pwd->pw_gecos[comma] != ','; ++comma) {}
			p->userName = std::string(pwd->pw_gecos, comma);
		}
		else
			p->userName = pwd->pw_name;
	}

	// Get documents path
	#ifdef OS_OSX
		std::string path = std::string(getenv("HOME")) + "/Documents";
		p->docsPath = path.c_str();
		p->gamePath = path.c_str();
		p->journal = "_______.app";
	#elif defined OS_LINUX
		char * path = xdg_user_dir_lookup("DOCUMENTS");
		p->docsPath = path;
		p->gamePath = path;
		p->journal = "_______";
	#endif
#endif

	Debug() << "Game path    :" << p->gamePath;
	Debug() << "Docs path    :" << p->docsPath;

#ifdef OS_LINUX
	char const* xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
	gtk_init(0, 0);

	if (xdg_current_desktop == NULL) {
		desktopEnv = "nope";
	} else {
		std::string desktop(xdg_current_desktop);
		std::transform(desktop.begin(), desktop.end(), desktop.begin(), ::tolower);
		if (desktop.find("cinnamon") != std::string::npos) {
			desktopEnv = "cinnamon";
		} else if (
			desktop.find("gnome") != std::string::npos ||
			desktop.find("unity") != std::string::npos
		) {
			desktopEnv = "gnome";
		} else if (desktop.find("mate") != std::string::npos) {
			desktopEnv = "mate";
		} else if (desktop.find("xfce") != std::string::npos) {
			desktopEnv = "xfce";
		} else if (desktop.find("kde") != std::string::npos) {
			desktopEnv = "kde";
		} else if (desktop.find("lxde") != std::string::npos) {
			desktopEnv = "lxde";
		} else if (desktop.find("deepin") != std::string::npos) {
			desktopEnv = "deepin";
		}
	}

	Debug() << "Desktop env  :" << desktopEnv;
#endif
}

Oneshot::~Oneshot()
{
	delete p;
}

const std::string &Oneshot::os() const
{
	return p->os;
}

const std::string &Oneshot::lang() const
{
	return p->lang;
}

const std::string &Oneshot::userName() const
{
	return p->userName;
}

const std::string &Oneshot::savePath() const
{
	return p->savePath;
}

const std::string &Oneshot::docsPath() const
{
	return p->docsPath;
}

const std::string &Oneshot::gamePath() const
{
	return p->gamePath;
}

const std::string &Oneshot::journal() const
{
	return p->journal;
}

bool Oneshot::exiting() const
{
	return p->exiting;
}

bool Oneshot::allowExit() const
{
	return p->allowExit;
}

void Oneshot::setYesNo(const char *yes, const char *no)
{
	p->txtYes = yes;
	p->txtNo = no;
}

void Oneshot::setExiting(bool exiting)
{
	if (p->exiting != exiting) {
		p->exiting = exiting;
		if (exiting) {
			threadData.exiting.set();
		} else {
			threadData.exiting.clear();
		}
	}
}

void Oneshot::setAllowExit(bool allowExit)
{
	if (p->allowExit != allowExit) {
		p->allowExit = allowExit;
		if (allowExit) {
			threadData.allowExit.set();
		} else {
			threadData.allowExit.clear();
		}
	}
}

bool Oneshot::msgbox(int type, const char *body, const char *title)
{
	if (title && !title[0])
#ifdef _WIN32
		title = "\u200b"; // Zero width space instead of filename in messagebox title
#else
		title = "";
#endif

#ifdef OS_LINUX
	linux_DialogData data = { type, body, title, 0 };
	gdk_threads_add_idle(linux_dialog, &data);
	gtk_main();
	return data.result;
#else
	// Buttons data
	static const SDL_MessageBoxButtonData buttonOk = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK" };
	static const SDL_MessageBoxButtonData buttonsOk[] = { buttonOk };
	SDL_MessageBoxButtonData buttonYes = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, p->txtYes.c_str() };
	SDL_MessageBoxButtonData buttonNo = { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, p->txtNo.c_str() };
	SDL_MessageBoxButtonData buttonsYesNo[] = { buttonNo, buttonYes };

	// Message box data
	SDL_MessageBoxData data;
	data.window = NULL; //p->window;
	data.colorScheme = 0;
	data.title = title;
	data.message = body;

	// Set message box type
	switch (type)
	{
		case MSG_INFO:
		case MSG_YESNO:
		default:
			data.flags = SDL_MESSAGEBOX_INFORMATION;
			break;
		case MSG_WARN:
			data.flags = SDL_MESSAGEBOX_WARNING;
			break;
		case MSG_ERR:
			data.flags = SDL_MESSAGEBOX_WARNING;
			break;
	}

	// Set message box buttons
	switch (type)
	{
		case MSG_INFO:
		case MSG_WARN:
		case MSG_ERR:
		default:
			data.numbuttons = 1;
			data.buttons = buttonsOk;
			break;
		case MSG_YESNO:
			data.numbuttons = 2;
			data.buttons = buttonsYesNo;
			break;
	}

	// Show message box
	int button;
	#ifdef OS_OSX
		int *btn = &button;
		// Message boxes and UI changes must be performed from the main thread
		// on macOS Mojave and above. This block ensures the message box
		// will show from the main thread.
		dispatch_sync(dispatch_get_main_queue(),
			^{ SDL_ShowMessageBox(&data, btn); }
		);
	#else
		SDL_ShowMessageBox(&data, &button);
	#endif

	return button ? true : false;
#endif
}

/* (unused and not really needed, commented for reference from mkxp-oneshot)
std::string Oneshot::textinput(const char* prompt, int char_limit, const char* fontName) {
	std::vector<std::string> *fontNames = new std::vector<std::string>();
	fontNames->push_back(fontName);
	fontNames->push_back("VL Gothic");
	Font *font = new Font(fontNames, 18);

	Bitmap *promptBmp = new Bitmap(DEF_SCREEN_W, DEF_SCREEN_H);
	promptBmp->setInitFont(font);
	promptBmp->drawText(0, 0, DEF_SCREEN_W, DEF_SCREEN_H, prompt, 1);

	Bitmap *inputBmp = new Bitmap(DEF_SCREEN_W, DEF_SCREEN_H);
	inputBmp->setInitFont(font);
	inputBmp->drawText(0, 0, DEF_SCREEN_W, DEF_SCREEN_H, "", 1);

	std::string inputTextPrev = std::string("");
	threadData.acceptingTextInput.set();
	threadData.inputTextLimit = char_limit;
	threadData.inputText.clear();
	SDL_StartTextInput();

	// Main loop
	while (threadData.acceptingTextInput) {
		if (inputTextPrev != threadData.inputText) {
			inputBmp->clear();
			inputBmp->drawText(DEF_SCREEN_W / 2, DEF_SCREEN_H / 2, DEF_SCREEN_W, DEF_SCREEN_H, threadData.inputText.c_str(), 1);
			inputTextPrev = threadData.inputText;
		}
	}

	// Disable text input
	SDL_StopTextInput();

	return threadData.inputText;
}
*/
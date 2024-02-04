#ifndef ONESHOT_H
#define ONESHOT_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "etc-internal.h"
#include <string>

struct OneshotPrivate;
struct RGSSThreadData;

class Oneshot {
public:
  Oneshot(RGSSThreadData &threadData);
  ~Oneshot();

  // msgbox type codes
  enum {
    MSG_INFO,
    MSG_YESNO,
    MSG_WARN,
    MSG_ERR,
  };
  
  // Accessors
  const std::string &os() const;
  const std::string &lang() const;
  const std::string &userName() const;
  const std::string &savePath() const;
  const std::string &docsPath() const;
  const std::string &gamePath() const;
  const std::string &journal() const;
  bool allowExit() const;
  bool exiting() const;

  // Mutators
  void setYesNo(const char *yes, const char *no);
  void setExiting(bool exiting);
  void setAllowExit(bool allowExit);

  // Functions
  bool msgbox(int type, const char *body, const char *title);
  std::string textinput(const char *prompt, int char_limit,
                        const char *fontName);

  // Dirty flag for obscured texture
  bool obscuredDirty;

#ifdef __linux__
  std::string desktopEnv;
#endif

private:
  OneshotPrivate *p;
  RGSSThreadData &threadData;
};

#endif // ONESHOT_H
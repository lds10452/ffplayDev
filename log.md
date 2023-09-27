##### sdl 1.2的版本编译

include中的SDL_config.h.default重命名为SDL_config.h

vs2019打开SDL_VS2010.sln解决方案，依次编译SDL和SDLmain生成lib和dll

##### ffmpeg项目编译

1.若出现error C4996: 'AVStream::codec': 被声明为已否决，按如下修改：

c/c++ 常规修改SDL检查为否(/sdl-)

2.相关依赖的dll库见picture目录

3.c++编译c代码

```
#include "SDL.h"
extern "C"
{
	#include "libavformat/avformat.h"
	#include "libavcodec/avcodec.h"
	#include "libswscale/swscale.h"
	#include "libavformat/avio.h"
	#include "libavutil/avstring.h"
	#include "libswresample/swresample.h"
	#include "libavutil/opt.h"
	#include "libavutil/time.h"
}
#pragma comment(lib,"SDL.lib")
#pragma comment(lib,"SDLmain.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avdevice.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"postproc.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"swscale.lib")
```


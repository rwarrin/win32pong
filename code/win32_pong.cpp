#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef int32_t int32;
typedef int16_t int16;
typedef int8_t int8;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;
typedef uint32_t bool32;
typedef float real32;
typedef double real64;

#define true 1
#define false 0

#define internal static
#define local_persist static
#define global_variable static

#define Assert(condition) if(!(condition)) { *(int32 *)0 = 0; }

#define ArrayCount(Array) (sizeof(Array) / (sizeof((Array)[0])))

global_variable bool32 GlobalRunning;
global_variable bool32 GlobalWindowResized;

union v2
{
	struct
	{
		real32 X, Y;
	};

	real32 E[2];
};

enum block_entity_type
{
	BlockEntity_Ball = 0,
	BlockEntity_Paddle,
	BlockEntity_PlayerGoalZone,
	BlockEntity_AIGoalZone,

	BlockEntity_Count
};

struct block_entity
{
	block_entity_type Type;
	v2 Position;
	v2 Velocity;
	uint32 Height;
	uint32 Width;
	bool32 Visible;
	uint32 Color;
};

struct game_data
{
	uint32 PlayerScore;
	uint32 AIScore;
	uint32 EntityCount;
	struct block_entity Entities[8];
	struct block_entity *Ball;
	struct block_entity *PlayerPaddle;
	struct block_entity *AIPaddle;
	struct block_entity *PlayerGoalZone;
	struct block_entity *AIGoalZone;
};

struct win32_screen_buffer
{
	BITMAPINFO Info;
	void *BitmapMemory;
	int Width;
	int Height;
	int Pitch;
	int BytesPerPixel;
};

// TODO(rick): Make this not a global variable
global_variable win32_screen_buffer GlobalBackBuffer;

struct win32_window_dimension
{
	int32 Width;
	int32 Height;
};

struct win32_window_dimension
Win32GetWindowDimensions(HWND Window)
{
	win32_window_dimension Result = {};

	RECT WindowRect = {};
	GetClientRect(Window, &WindowRect);
	Result.Width = WindowRect.right - WindowRect.left;
	Result.Height = WindowRect.bottom - WindowRect.top;

	return(Result);
}

static void
Win32ResizeDIBSection(win32_screen_buffer *BackBuffer, int Width, int Height)
{
	if(BackBuffer->BitmapMemory)
	{
		VirtualFree(BackBuffer->BitmapMemory, 0, MEM_RELEASE);
	}

	BackBuffer->Width = Width;
	BackBuffer->Height = Height;
	BackBuffer->BytesPerPixel = 4;

	BackBuffer->Info.bmiHeader.biSize = sizeof(BackBuffer->Info.bmiHeader);
	BackBuffer->Info.bmiHeader.biWidth = Width;
	BackBuffer->Info.bmiHeader.biHeight = -Height;
	BackBuffer->Info.bmiHeader.biPlanes = 1;
	BackBuffer->Info.bmiHeader.biBitCount = 32;
	BackBuffer->Info.bmiHeader.biCompression = BI_RGB;

	int BitmapMemorySize = (BackBuffer->Width * BackBuffer->Height) * BackBuffer->BytesPerPixel;
	BackBuffer->BitmapMemory = (void *)VirtualAlloc(0, BitmapMemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	BackBuffer->Pitch = Width * BackBuffer->BytesPerPixel;
}

internal void
Win32DisplayBufferInWindow(win32_screen_buffer *Buffer, HDC DeviceContext,
						   int WindowWidth, int WindowHeight)
{
	StretchDIBits(DeviceContext,
				  0, 0, Buffer->Width, Buffer->Height,
				  0, 0, Buffer->Width, Buffer->Height,
				  Buffer->BitmapMemory,
				  &Buffer->Info,
				  DIB_RGB_COLORS, SRCCOPY);
}

internal void
Win32ClearScreenToBlack(win32_screen_buffer *Buffer)
{
	for(uint32 Y = 0; Y < Buffer->Height; ++Y)
	{
		for(uint32 X = 0; X < Buffer->Width; ++X)
		{
			uint32 *Pixel = (uint32 *)((uint8 *)Buffer->BitmapMemory + (Y * Buffer->Pitch) + (X * Buffer->BytesPerPixel));
			if(Buffer->Width / 2 == X)
			{
				*Pixel = 0xffffffff;
			}
			else
			{
				*Pixel = 0xff000000;
			}
		}
	}
}

internal void
ConstrainEntityMovement(win32_screen_buffer *Buffer, struct block_entity *Entity)
{
	if(Entity->Type == BlockEntity_Ball)
	{
		if(Entity->Position.Y < 0)
		{
			Entity->Position.Y = 0;
			Entity->Velocity.Y = Entity->Velocity.Y * -1;
		}
		else if((Entity->Position.Y + Entity->Height) > Buffer->Height)
		{
			Entity->Position.Y = Buffer->Height - Entity->Height;
			Entity->Velocity.Y = Entity->Velocity.Y * -1;
		}
	}
	else
	{
		if(Entity->Position.Y < 0)
		{
			Entity->Position.Y = 0;
		}
		else if((Entity->Position.Y + Entity->Height) > Buffer->Height)
		{
			Entity->Position.Y = Buffer->Height - Entity->Height;
		}
	}

	if(Entity->Position.X < 0)
	{
		Entity->Position.X = 0;
	}
	else if((Entity->Position.X + Entity->Width) > Buffer->Width)
	{
		Entity->Position.X = Buffer->Width - Entity->Width;
	}
}

internal void
Win32DrawRectangle(win32_screen_buffer *Buffer, int32 MinX, int32 MinY,
				   int32 MaxX, int32 MaxY, uint32 Color)
{
	if(MinX < 0) { MinX = 0; }
	if(MaxX > Buffer->Width) { MaxX = Buffer->Width; }
	if(MinY < 0) { MinY = 0; }
	if(MaxY > Buffer->Height) { MaxY = Buffer->Height; }

	for(uint32 Y = MinY; Y < MaxY; ++Y)
	{
		uint32 *Pixel = (uint32 *)((uint8 *)Buffer->BitmapMemory + (Y * Buffer->Pitch) + (MinX * Buffer->BytesPerPixel));
		for(uint32 X = MinX; X < MaxX; ++X)
		{
			*Pixel++ = Color;
		}
	}
}

internal void
Win32DrawText(struct win32_screen_buffer *Buffer, char *Text, uint32 Length,
			  uint32 ScreenX, uint32 ScreenY,
			  uint32 Width, uint32 Height, uint32 Color, bool32 RightToLeft = false)
{
	uint32 TextBitmapWidth = 128;
	uint32 TextBitmapHeight= 64;

	static HDC TextDeviceContext = 0;
	if(TextDeviceContext == 0)
	{
		TextDeviceContext = CreateCompatibleDC(0);
		HBITMAP TextBitmap = CreateCompatibleBitmap(TextDeviceContext, TextBitmapWidth, TextBitmapHeight);
		if(TextBitmap != 0)
		{
			SelectObject(TextDeviceContext, TextBitmap);
		}
	}

	SIZE TextSize = {};
	GetTextExtentPoint32(TextDeviceContext, Text, Length, &TextSize);
	if(RightToLeft == true)
	{
		ScreenX -= TextSize.cx;
	}

	SetBkColor(TextDeviceContext, RGB(0, 0, 0));
	// TODO(rick): Why is this still white?
	SetTextColor(TextDeviceContext, RGB(((0xff << 16) & Color) >> 16, ((0xff << 8) & Color) >> 8, (0xff & Color)));
	TextOut(TextDeviceContext, 0, 0, Text, Length);

	for(uint32 SrcY = 0, DestY = ScreenY;
		SrcY < TextSize.cy;
		++SrcY, ++DestY)
	{
		for(uint32 SrcX = 0, DestX = ScreenX;
			SrcX < TextSize.cx;
			++SrcX, ++DestX)
		{
			COLORREF SrcPixel = GetPixel(TextDeviceContext, SrcX, SrcY);
			uint32 *DestPixel = (uint32 *)((uint8 *)Buffer->BitmapMemory + (DestY * Buffer->Pitch) + (DestX * Buffer->BytesPerPixel));
			*DestPixel = SrcPixel;
		}
	}
}

internal struct block_entity *
AddBlockEntity(struct game_data *GameData, block_entity_type Type,
			   int X, int Y, uint32 Width, uint32 Height,
			   bool32 Visible, uint32 Color)
{
	Assert(GameData->EntityCount < ArrayCount(GameData->Entities));

	struct block_entity *Result = 0;
	if(GameData->EntityCount < ArrayCount(GameData->Entities))
	{
		Result = (GameData->Entities + GameData->EntityCount++);
		Result->Type = Type;
		Result->Position.X = X;
		Result->Position.Y = Y;
		Result->Width = Width;
		Result->Height = Height;
		Result->Visible = Visible;
		Result->Color = Color;
	}

	return(Result);
}

internal bool32
TestCollision(int32 ColliderX, int32 ColliderY,
			  int32 CollisionMinX, int32 CollisionMinY,
			  int32 CollisionMaxX, int32 CollisionMaxY)
{
	bool32 Result = false;

	Result = ( ((ColliderX > CollisionMinX) && (ColliderX < CollisionMaxX)) &&
			   ((ColliderY > CollisionMinY) && (ColliderY < CollisionMaxY)) );

	return(Result);
}

internal void
CollisionDetection(struct game_data *GameData, struct win32_screen_buffer *Buffer)
{
	for(uint32 EntityIndex = 0;
		EntityIndex < GameData->EntityCount;
		++EntityIndex)
	{
		struct block_entity *Entity = GameData->Entities + EntityIndex;
		if(Entity->Type == BlockEntity_Ball)
		{
			for(uint32 CollisionEntityIndex = 0;
				CollisionEntityIndex < GameData->EntityCount;
				++CollisionEntityIndex)
			{
				struct block_entity *CollisionEntity = GameData->Entities + CollisionEntityIndex;
				if(Entity != CollisionEntity)
				{
					real32 EntityHalfWidth = Entity->Width / 2.0f;
					real32 EntityHalfHeight = Entity->Height / 2.0f;

					bool32 Collided = TestCollision(Entity->Position.X + EntityHalfWidth,
													Entity->Position.Y + EntityHalfHeight,
													CollisionEntity->Position.X - EntityHalfWidth,
													CollisionEntity->Position.Y - EntityHalfHeight,
													CollisionEntity->Position.X + CollisionEntity->Width + EntityHalfWidth,
													CollisionEntity->Position.Y + CollisionEntity->Height + EntityHalfHeight);

					if(Collided)
					{
						if(CollisionEntity->Type == BlockEntity_Paddle)
						{
							Entity->Velocity.X *= -1.75f;
							Entity->Velocity.Y += CollisionEntity->Velocity.Y;

							if(Entity->Velocity.X > 8.5f) { Entity->Velocity.X = 8.5f; }
							else if(Entity->Velocity.X < -8.5f) { Entity->Velocity.X = -8.5f; }
							if(Entity->Velocity.Y > 5.0f) { Entity->Velocity.Y = 5.0f; }
							else if(Entity->Velocity.Y < -5.0f) { Entity->Velocity.Y = -5.0f; }
						}
						else if((CollisionEntity->Type == BlockEntity_PlayerGoalZone) ||
								(CollisionEntity->Type == BlockEntity_AIGoalZone))
						{
							GameData->PlayerPaddle->Position.Y = (Buffer->Height / 2) - (GameData->PlayerPaddle->Height / 2);
							GameData->AIPaddle->Position.Y = (Buffer->Height / 2) - (GameData->AIPaddle->Height / 2);
							GameData->Ball->Position.X = (Buffer->Width / 2) - (GameData->Ball->Width / 2);
							GameData->Ball->Position.Y = (Buffer->Height / 2) - (GameData->Ball->Height / 2);
							GameData->Ball->Velocity.Y = 0.0f;

							if(CollisionEntity->Type == BlockEntity_PlayerGoalZone)
							{
								GameData->Ball->Velocity.X = 4.0f;
								++GameData->AIScore;
							}
							else if(CollisionEntity->Type == BlockEntity_AIGoalZone)
							{
								GameData->Ball->Velocity.X = -4.0f;
								++GameData->PlayerScore;
							}
						}
					}
				}
			}
		}
	}
}

internal void
ApplyAIPaddleLogic(struct win32_screen_buffer *Buffer, struct block_entity *Paddle, struct block_entity *Ball)
{
	real32 PaddleMidPointY = Paddle->Position.Y + (Paddle->Height / 2.0f);
	real32 PaddleMidPointX = Paddle->Position.X + (Paddle->Width / 2.0f);
	real32 BallMidPointX = Ball->Position.X + (Ball->Width / 2.0f);
	real32 BallMidPointY = Ball->Position.Y + (Ball->Height / 2.0f);
	real32 InaccuracyEpsilon = Paddle->Height / 8.0f;

	real32 DistanceToMiddle = 0.0f;
	real32 AbsDistanceToMiddle = 0.0f;

	if((BallMidPointX > Buffer->Width / 2.0f) &&
	   (BallMidPointX < PaddleMidPointX))
	{
		DistanceToMiddle = BallMidPointY - PaddleMidPointY;
	}
	else
	{
		DistanceToMiddle = (Buffer->Height / 2.0f) - PaddleMidPointY;
	}

	AbsDistanceToMiddle = (DistanceToMiddle < 0.0f ? -DistanceToMiddle : DistanceToMiddle);
	if(AbsDistanceToMiddle > InaccuracyEpsilon)
	{
		if(DistanceToMiddle > 0.0f)
		{
			Paddle->Velocity.Y = 4.5f;
		}
		else
		{
			Paddle->Velocity.Y = -4.5f;
		}
	}
	else
	{
		Paddle->Velocity.Y = 0.0f;
	}
}

LRESULT CALLBACK
WindowsCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	LRESULT Result = 0;

	switch(Message)
	{
		case WM_DESTROY:
		case WM_QUIT:
		{
			GlobalRunning = false;
			PostQuitMessage(0);
		} break;
		case WM_SIZE:
		{
			win32_window_dimension WindowDims = Win32GetWindowDimensions(Window);
			Win32ResizeDIBSection(&GlobalBackBuffer, WindowDims.Width, WindowDims.Height);
			GlobalWindowResized = true;
		} break;
		default:
		{
			Result = DefWindowProc(Window, Message, WParam, LParam);
		} break;
	}

	return(Result);
}

static void
ProcessPendingMessages(struct block_entity *PlayerPaddle)
{
	MSG Message;
	while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
	{
		switch(Message.message)
		{
			case WM_KEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			{
				uint32 KeyCode = (uint32)Message.wParam;
				uint32 WasDown = ((Message.lParam & (1 << 30)) != 0);
				uint32 IsDown = ((Message.lParam & (1 << 31)) == 0);

				if((KeyCode == 'W') || (KeyCode == VK_UP))
				{
					if(IsDown) { PlayerPaddle->Velocity.Y = -7.5f; }
					else if(WasDown) { PlayerPaddle->Velocity.Y = 0.0f; }
				}
				if((KeyCode == 'S') || (KeyCode == VK_DOWN))
				{
					if(IsDown) { PlayerPaddle->Velocity.Y = 7.5f; }
					else if(WasDown) { PlayerPaddle->Velocity.Y = 0.0f; }
				}
				if(KeyCode == VK_ESCAPE)
				{
					GlobalRunning = false;
				}
			} break;
			default:
			{
				TranslateMessage(&Message);
				DispatchMessage(&Message);
			} break;
		}
	}
}

int WINAPI
WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CmdLine, int CmdShow)
{
	WNDCLASSEXA WindowClass = {};
	WindowClass.cbSize = sizeof(WNDCLASSEXA);
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	WindowClass.lpfnWndProc = WindowsCallback;
	WindowClass.hInstance = Instance;
	WindowClass.lpszClassName = "Win32_Pong";

	if(!RegisterClassEx(&WindowClass))
	{
		MessageBox(NULL, "Error", "Failed to register window class.", MB_OK);
		return(-1);
	}

	HWND Window = CreateWindowEx(0,
								 WindowClass.lpszClassName,
								 "Win32_Pong",
								 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
								 CW_USEDEFAULT, CW_USEDEFAULT,
								 640, 480,
								 0, 0, Instance, 0);
	if(!Window)
	{
		MessageBox(NULL, "Error", "Failed to create window.", MB_OK);
		return(-2);
	}

	real32 TargetFPS = 60.0f;
	real32 TargetSecondsPerFrame = 1.0f / TargetFPS;
	uint32 TargetMSPerFrame = (uint32)(TargetSecondsPerFrame * 1000);

	LARGE_INTEGER StartTime = {};
	LARGE_INTEGER EndTime = {};
	LARGE_INTEGER CPUFrequency = {};
	real64 ElapsedTimeForFrame = {};

	win32_window_dimension WindowDims = Win32GetWindowDimensions(Window);
	Win32ResizeDIBSection(&GlobalBackBuffer, WindowDims.Width, WindowDims.Height);

	uint32 PaddleWidth = 8;
	uint32 PaddleHeight = 50;
	uint32 BallWidth = 10;
	uint32 BallHeight = 10;
	uint32 GoalZoneWidth = 10;
	uint32 GoalZoneHeight = GlobalBackBuffer.Height;

	struct game_data GameData = {};
	GameData.Ball = AddBlockEntity(&GameData, BlockEntity_Ball,
											   ((GlobalBackBuffer.Width / 2) - (BallWidth / 2)),
											   ((GlobalBackBuffer.Height / 2) - (BallHeight / 2)),
											   BallWidth, BallHeight, true, 0xffffffff);
	GameData.PlayerPaddle = AddBlockEntity(&GameData, BlockEntity_Paddle,
													   20,
													   ((GlobalBackBuffer.Height / 2) - (PaddleHeight / 2)),
													   PaddleWidth, PaddleHeight, true, 0xffffffff);
	GameData.AIPaddle = AddBlockEntity(&GameData, BlockEntity_Paddle,
												   (GlobalBackBuffer.Width - 20 - PaddleWidth),
												   ((GlobalBackBuffer.Height / 2) - (PaddleHeight / 2)),
												   PaddleWidth, PaddleHeight, true, 0xffffffff);
	GameData.PlayerGoalZone = AddBlockEntity(&GameData, BlockEntity_PlayerGoalZone,
														 0, 0, GoalZoneWidth, GoalZoneHeight, false, 0xffff0000);
	GameData.AIGoalZone = AddBlockEntity(&GameData, BlockEntity_AIGoalZone,
													 GlobalBackBuffer.Width - GoalZoneWidth, 0,
													 GoalZoneWidth, GoalZoneHeight, false, 0xffff0000);

	GameData.Ball->Velocity.X = -4.0f;
	GameData.Ball->Velocity.Y = 0.0f;

	GlobalRunning = true;
	while(GlobalRunning)
	{
		ProcessPendingMessages(GameData.PlayerPaddle);

		QueryPerformanceFrequency(&CPUFrequency);
		QueryPerformanceCounter(&StartTime);

		if(GlobalWindowResized)
		{
			// TODO(rick): Check the math on this, the Player paddle did not
			// stay centered when resizing
			win32_window_dimension NewWindowDims = Win32GetWindowDimensions(Window);
			real32 DeltaWidth = (real32)NewWindowDims.Width / (real32)WindowDims.Width;
			real32 DeltaHeight = (real32)NewWindowDims.Height / (real32)WindowDims.Height;

			for(uint32 EntityIndex = 0;
				EntityIndex < GameData.EntityCount;
				++EntityIndex)
			{
				struct block_entity *Entity = GameData.Entities + EntityIndex;
				if(Entity->Type == BlockEntity_Ball)
				{
					Entity->Position.X *= DeltaWidth;
					Entity->Position.Y *= DeltaHeight;
				}
				else if(Entity->Type == BlockEntity_Paddle)
				{
					Entity->Position.Y *= DeltaHeight;
					// TODO(rick): There has to be a better way to do this
					if(Entity == GameData.AIPaddle)
					{
						Entity->Position.X = GlobalBackBuffer.Width - Entity->Width - 20;
					}
				}
				else if(Entity->Type == BlockEntity_PlayerGoalZone)
				{
					Entity->Height = GlobalBackBuffer.Height;
				}
				else if(Entity->Type == BlockEntity_AIGoalZone)
				{
					Entity->Position.X = GlobalBackBuffer.Width - Entity->Width;
					Entity->Height = GlobalBackBuffer.Height;
				}
			}
			
			GlobalWindowResized = false;
		}

		ApplyAIPaddleLogic(&GlobalBackBuffer, GameData.AIPaddle, GameData.Ball);
		for(uint32 EntityIndex = 0;
			EntityIndex < ArrayCount(GameData.Entities);
			++EntityIndex)
		{
			struct block_entity *Entity = &GameData.Entities[EntityIndex];
			Entity->Position.X += Entity->Velocity.X;
			Entity->Position.Y += Entity->Velocity.Y;
			ConstrainEntityMovement(&GlobalBackBuffer, Entity);
		}
		CollisionDetection(&GameData, &GlobalBackBuffer);

		Win32ClearScreenToBlack(&GlobalBackBuffer);

		uint32 ScreenMiddle = GlobalBackBuffer.Width / 2;
		uint32 ScoreCharacterCount = 0;
		char ScoreText[10] = {0};

		ScoreCharacterCount = _snprintf(ScoreText, 10, "%0d", GameData.PlayerScore);
		Win32DrawText(&GlobalBackBuffer, ScoreText, ScoreCharacterCount,
					  ScreenMiddle - 10, 10,
					  80, 80, 0xff55ff99, true);

		ScoreCharacterCount = _snprintf(ScoreText, 10, "%0d", GameData.AIScore);
		Win32DrawText(&GlobalBackBuffer, ScoreText, ScoreCharacterCount,
					  ScreenMiddle + 10, 10,
					  80, 80, 0xff55ff99, false);

		for(uint32 EntityIndex = 0;
			EntityIndex < ArrayCount(GameData.Entities);
			++EntityIndex)
		{
			struct block_entity *Entity = &GameData.Entities[EntityIndex];
			if(Entity->Visible)
			{
				Win32DrawRectangle(&GlobalBackBuffer, Entity->Position.X, Entity->Position.Y,
								Entity->Position.X + Entity->Width, Entity->Position.Y + Entity->Height,
								Entity->Color);
			}
		}

		HDC DeviceContext = GetDC(Window);
		WindowDims = Win32GetWindowDimensions(Window);
		Win32DisplayBufferInWindow(&GlobalBackBuffer, DeviceContext, WindowDims.Width, WindowDims.Height);
		ReleaseDC(Window, DeviceContext);

		QueryPerformanceCounter(&EndTime);
		ElapsedTimeForFrame = (real64)((EndTime.QuadPart - StartTime.QuadPart)) / (real64)CPUFrequency.QuadPart;

		if(ElapsedTimeForFrame < TargetSecondsPerFrame)
		{
			real32 MSToSleep = (TargetSecondsPerFrame - ElapsedTimeForFrame) * 1000.0f;
#if 0
			char temp[1000] = {0};
			int len = _snprintf(temp, 1000, "Elapsed: %.02fms | Sleeping: %.02fms", ElapsedTimeForFrame * 1000.0f, MSToSleep);
			TextOut(DeviceContext, 10, 10, temp, len);
			ReleaseDC(Window, DeviceContext);
#endif
			Sleep(MSToSleep);
		}
	}

	return(0);
}


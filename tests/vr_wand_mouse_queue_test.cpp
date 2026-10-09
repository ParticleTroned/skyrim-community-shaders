#include "Features/VR/WandMouseEventQueue.h"

#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <cstdlib>

namespace
{
	struct Pointer
	{
		int controller = 0;
		ImVec2 position{};
	};
	using Queue = WandMouseEventQueue<Pointer, ImGuiMouseButton_COUNT>;

	bool Check(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			std::printf("FAIL: %s\n", a_message);
		return a_condition;
	}

	void Dispatch(const Queue::Event& a_event)
	{
		auto& io = ImGui::GetIO();
		io.AddMousePosEvent(a_event.pointer.position.x, a_event.pointer.position.y);
		io.AddMouseButtonEvent(a_event.button, a_event.down);
	}

	bool Run()
	{
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(1280, 720);
		io.ConfigInputTrickleEventQueue = false;
		io.Fonts->AddFontDefault();
		io.Fonts->Build();

		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ImGui::NewFrame();
		bool passed = Check(!ImGui::IsMouseClicked(ImGuiMouseButton_Left),
			"baseline must reproduce a lost press/release in the same ImGui batch");
		ImGui::EndFrame();

		Queue queue;
		const Pointer first{ 1, ImVec2(120, 200) };
		const Pointer second{ 2, ImVec2(840, 300) };
		queue.Push({ ImGuiMouseButton_Left, true, first });
		passed &= Check(!queue.Push({ ImGuiMouseButton_Left, true, first }), "repeated held samples must not create a backlog");
		passed &= Check(!queue.Push({ -1, true, first }), "invalid mouse buttons must not enter the queue");
		queue.Push({ ImGuiMouseButton_Left, false, first });
		queue.Push({ ImGuiMouseButton_Left, true, second });
		queue.Push({ ImGuiMouseButton_Left, false, second });
		passed &= Check(queue.HasPendingPress(ImGuiMouseButton_Left, [&](const Pointer& pointer) {
			return pointer.controller == first.controller;
		}) && queue.HasPendingPress(ImGuiMouseButton_Left, [&](const Pointer& pointer) {
			return pointer.controller == second.controller;
		}),
			"a later hand tap must not hide an earlier pending owner during cancellation");
		int clicks = 0;
		int releases = 0;
		for (int step = 0; step < 4; ++step) {
			const int frame = ImGui::GetFrameCount() + 1;
			const auto event = queue.PopForFrame(frame);
			passed &= Check(event.has_value(), "every queued tap edge must be retained");
			passed &= Check(!queue.PopForFrame(frame), "one render frame must not consume a second edge");
			if (event)
				Dispatch(*event);
			ImGui::NewFrame();
			const Pointer& expected = step < 2 ? first : second;
			passed &= Check(event && event->pointer.controller == expected.controller &&
								io.MousePos.x == expected.position.x && io.MousePos.y == expected.position.y,
				"each click and release must keep its original controller and cursor");
			clicks += ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? 1 : 0;
			releases += ImGui::IsMouseReleased(ImGuiMouseButton_Left) ? 1 : 0;
			ImGui::EndFrame();
		}
		passed &= Check(clicks == 2 && releases == 2, "two fast taps must produce two complete ImGui clicks");
		passed &= Check(!queue.PopForFrame(ImGui::GetFrameCount() + 1), "all tap edges must drain");

		queue.Push({ ImGuiMouseButton_Left, true, first });
		passed &= Check(queue.HasPendingPress(ImGuiMouseButton_Left) &&
							!queue.HasPendingPress(ImGuiMouseButton_Right),
			"pending press detection must match its button");
		Dispatch(*queue.PopForFrame(ImGui::GetFrameCount() + 1));
		ImGui::NewFrame();
		passed &= Check(ImGui::IsMouseClicked(ImGuiMouseButton_Left), "drag must start with a press");
		ImGui::EndFrame();
		io.AddMousePosEvent(480, 400);
		ImGui::NewFrame();
		passed &= Check(io.MouseDown[ImGuiMouseButton_Left] && ImGui::IsMouseDragging(ImGuiMouseButton_Left),
			"a held trigger must continue dragging without queued pose samples");
		ImGui::EndFrame();
		queue.Push({ ImGuiMouseButton_Left, false, { 1, ImVec2(480, 400) } });
		Dispatch(*queue.PopForFrame(ImGui::GetFrameCount() + 1));
		ImGui::NewFrame();
		passed &= Check(ImGui::IsMouseReleased(ImGuiMouseButton_Left) && io.MousePos.x == 480 && io.MousePos.y == 400,
			"a drag release must use the displayed release position");
		ImGui::EndFrame();

		const auto drawButton = [] {
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(640, 480));
			ImGui::Begin("Wand release target", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
			ImGui::SetCursorScreenPos(ImVec2(100, 100));
			const bool activated = ImGui::Button("Click target", ImVec2(160, 80));
			ImGui::End();
			return activated;
		};
		for (int frame = 0; frame < 2; ++frame) {
			io.AddMousePosEvent(140, 140);
			ImGui::NewFrame();
			drawButton();
			ImGui::EndFrame();
		}
		queue.Push({ ImGuiMouseButton_Left, true, { 1, ImVec2(140, 140) } });
		Dispatch(*queue.PopForFrame(ImGui::GetFrameCount() + 1));
		ImGui::NewFrame();
		passed &= Check(!drawButton() && io.MouseDown[ImGuiMouseButton_Left], "button must await release after pointer press");
		ImGui::EndFrame();
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		ImGui::NewFrame();
		passed &= Check(!drawButton(), "tracking loss must not activate a held button");
		const Pointer missedPointer{ 1, io.MousePos };
		ImGui::EndFrame();
		queue.Push({ ImGuiMouseButton_Left, false, missedPointer });
		Dispatch(*queue.PopForFrame(ImGui::GetFrameCount() + 1));
		ImGui::NewFrame();
		passed &= Check(!drawButton() && !io.MouseDown[ImGuiMouseButton_Left],
			"release after leaving the surface must cancel activation and release the button");
		ImGui::EndFrame();

		const auto beginCancellationPress = [&] {
			io.AddMousePosEvent(140, 140);
			ImGui::NewFrame();
			drawButton();
			ImGui::EndFrame();
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			ImGui::NewFrame();
			const bool activated = drawButton();
			const bool down = io.MouseDown[ImGuiMouseButton_Left];
			ImGui::EndFrame();
			return !activated && down;
		};
		passed &= Check(beginCancellationPress(), "cancellation regression must begin with a held widget");
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		io.AddMousePosEvent(140, 140);
		ImGui::NewFrame();
		passed &= Check(drawButton(), "baseline must reproduce restored desktop hover activating a canceled release");
		ImGui::EndFrame();

		passed &= Check(beginCancellationPress(), "guarded cancellation must begin with a held widget");
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		io.AddMousePosEvent(140, 140);
		// The cancellation guard must be the final position before NewFrame.
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		ImGui::NewFrame();
		passed &= Check(!drawButton() && !io.MouseDown[ImGuiMouseButton_Left],
			"a canceled lease must remain off-surface after desktop input attempts to restore hover");
		ImGui::EndFrame();

		passed &= Check(beginCancellationPress(), "same-frame miss must begin with a held button");
		const Pointer previousHit{ 1, ImVec2(140, 140) };
		queue.Push({ ImGuiMouseButton_Left, true, previousHit });
		queue.PopForFrame(ImGui::GetFrameCount());
		queue.Push({ ImGuiMouseButton_Left, false, previousHit });
		Dispatch(*queue.PopForFrame(ImGui::GetFrameCount() + 1));
		ImGui::NewFrame();
		passed &= Check(drawButton(), "baseline stale release position reproduces activation on a same-frame miss");
		ImGui::EndFrame();
		passed &= Check(beginCancellationPress(), "fresh miss release must begin with a held button");
		queue.Push({ ImGuiMouseButton_Left, true, previousHit });
		queue.PopForFrame(ImGui::GetFrameCount());
		queue.Push({ ImGuiMouseButton_Left, false, { 1, ImVec2(-FLT_MAX, -FLT_MAX) } });
		Dispatch(*queue.PopForFrame(ImGui::GetFrameCount() + 1));
		ImGui::NewFrame();
		passed &= Check(!drawButton() && !io.MouseDown[ImGuiMouseButton_Left],
			"current off-surface release cancels even when the previous presented pointer was a hit");
		ImGui::EndFrame();

		queue.Push({ ImGuiMouseButton_Right, true, second });
		queue.Push({ ImGuiMouseButton_Right, false, second });
		queue.Clear();
		passed &= Check(!queue.PopForFrame(ImGui::GetFrameCount() + 1) &&
							!queue.HasPendingPress(ImGuiMouseButton_Right),
			"reset must discard stale edges and ownership");
		for (std::size_t edge = 0; edge < Queue::MaximumPendingEvents; ++edge)
			passed &= Check(queue.Push({ ImGuiMouseButton_Left, edge % 2 == 0, first }),
				"bounded input queue must retain accepted edges");
		passed &= Check(!queue.Push({ ImGuiMouseButton_Left, true, second }) &&
							!queue.HasPendingPress(ImGuiMouseButton_Left) && !queue.PopForFrame(ImGui::GetFrameCount() + 1),
			"overflow must discard stale presses and fail closed for caller cancellation");
		passed &= Check(queue.Push({ ImGuiMouseButton_Left, true, second }),
			"overflow must reset queued button state for a fresh input session");
		queue.Clear();
		ImGui::DestroyContext();
		return passed;
	}
}

int main()
{
	if (!Run())
		return EXIT_FAILURE;
	std::puts("Wand queue preserves rapid taps, pointer ownership, dragging, and reset with ImGui trickling disabled.");
	return EXIT_SUCCESS;
}

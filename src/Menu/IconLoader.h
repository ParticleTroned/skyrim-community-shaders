#pragma once

struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct ImVec2;
class Menu;

namespace Util
{
	namespace IconLoader
	{
		bool LoadTextureFromFile(ID3D11Device* device, const char* filename, ID3D11ShaderResourceView** out_srv, ImVec2& out_size);
		bool InitializeMenuIcons(Menu* menu);
	}
}

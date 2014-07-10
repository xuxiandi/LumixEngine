#pragma once


#include "core/lumix.h"
#include "engine/iplugin.h"
#include "gui/block.h"
#include "gui/irenderer.h"

namespace Lumix
{

namespace FS
{
	class IFile;
}

namespace UI
{

	class Atlas;
	class Block;


	class LUMIX_GUI_API Gui : public IPlugin
	{
		public:
			typedef Delegate<void (int, int)> MouseCallback;
			typedef Delegate<void (int, int, int, int)> MouseMoveCallback;

		public:
			Gui() { m_impl = 0; }

			virtual bool create(Engine& engine) override;
			virtual void destroy() override;
			virtual Component createComponent(uint32_t, const Entity&) override;
			virtual const char* getName() const override { return "gui"; }

			void setRenderer(IRenderer& renderer);
			IRenderer& getRenderer();
			void createBaseDecorators(const char* atlas_path);
			void render();
			void layout();
			Block* createTopLevelBlock(float width, float height);
			void focus(Block* block);
			Block* getFocusedBlock() const;
			bool click(int x, int y);
			void mouseDown(int x, int y);
			void mouseMove(int x, int y, int rel_x, int rel_y);
			void mouseUp(int x, int y);
			void keyDown(int32_t key);
			DecoratorBase* getDecorator(const char* name);
			void addDecorator(DecoratorBase& decorator);
			Atlas* loadAtlas(const char* path);
			Block* createGui(Lumix::FS::IFile& file);
			Block* createBlock(uint32_t type, Block* parent);
			Block* getBlock(int x, int y);
			MouseMoveCallback& addMouseMoveCallback();
			MouseCallback& addMouseUpCallback();
			void removeMouseMoveCallback(MouseMoveCallback& callback);
			void removeMouseUpCallback(MouseCallback& callback);

		private:
			struct GuiImpl* m_impl;
	};


} // ~namespace UI
} // ~namespace Lumix

#pragma once


#include "gui/block.h"


namespace Lumix
{
namespace UI
{

	class Scrollbar;

	class LUMIX_GUI_API Scrollable : public Block
	{
		public:
			Scrollable(Gui& gui, Block* parent);
			virtual ~Scrollable();
			virtual uint32_t getType() const override;
			virtual void serialize(ISerializer& serializer) override;
			virtual void deserialize(ISerializer& serializer) override;
			virtual void layout() override;
			Lumix::UI::Block* getContainer() const { return m_container; } 

		private:
			void scrollbarValueChanged(Block& block, void*);

		private:
			Scrollbar* m_horizontal_scrollbar;
			Scrollbar* m_vertical_scrollbar;
			Lumix::UI::Block* m_container;
	};


} // ~namespace UI
} // ~namespace Lumix

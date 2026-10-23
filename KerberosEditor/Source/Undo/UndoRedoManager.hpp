#pragma once

#include "ICommand.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Kerberos {

	class UndoRedoManager
	{
	public:
		/// Executes the command and records it in the undo history.
		void Execute(std::unique_ptr<ICommand> command);

		/// Records a command whose effect was already applied by the caller,
		/// which is the common case for ImGui widgets that write into components.
		void Record(std::unique_ptr<ICommand> command);

		void Undo();
		void Redo();

		bool CanUndo() const;
		bool CanRedo() const;

		/// Returns the label of the next command to undo, or an empty string.
		const std::string& GetUndoName() const;

		/// Returns the label of the next command to redo, or an empty string.
		const std::string& GetRedoName() const;

		void Clear();

	private:
		using CommandContainer = std::vector<std::unique_ptr<ICommand>>;

		void Push(std::unique_ptr<ICommand> command);
		void Trim();

		CommandContainer m_UndoStack;
		CommandContainer m_RedoStack;

		std::size_t m_MaxHistory = 256;
	};

}

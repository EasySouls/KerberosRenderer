#include "UndoRedoManager.hpp"

#include <utility>

namespace Kerberos {

	void UndoRedoManager::Execute(std::unique_ptr<ICommand> command)
	{
		if (!command)
			return;

		command->Execute();
		Push(std::move(command));
	}

	void UndoRedoManager::Record(std::unique_ptr<ICommand> command)
	{
		if (!command)
			return;

		Push(std::move(command));
	}

	void UndoRedoManager::Undo()
	{
		if (!CanUndo())
			return;

		std::unique_ptr<ICommand>& command = m_UndoStack.back();
		command->Undo();
		m_RedoStack.push_back(std::move(command));
		m_UndoStack.pop_back();
	}

	void UndoRedoManager::Redo()
	{
		if (!CanRedo())
			return;

		std::unique_ptr<ICommand>& command = m_RedoStack.back();
		command->Execute();
		m_UndoStack.push_back(std::move(command));
		m_RedoStack.pop_back();
	}

	bool UndoRedoManager::CanUndo() const
	{
		return !m_UndoStack.empty();
	}

	bool UndoRedoManager::CanRedo() const
	{
		return !m_RedoStack.empty();
	}

	const std::string& UndoRedoManager::GetUndoName() const
	{
		static const std::string emptyName;
		return m_UndoStack.empty() ? emptyName : m_UndoStack.back()->GetName();
	}

	const std::string& UndoRedoManager::GetRedoName() const
	{
		static const std::string emptyName;
		return m_RedoStack.empty() ? emptyName : m_RedoStack.back()->GetName();
	}

	void UndoRedoManager::Clear()
	{
		m_UndoStack.clear();
		m_RedoStack.clear();
	}

	void UndoRedoManager::Push(std::unique_ptr<ICommand> command)
	{
		if (!m_UndoStack.empty() && m_UndoStack.back()->CanMerge(command.get()) &&
			m_UndoStack.back()->Merge(command.get()))
		{
			m_RedoStack.clear();
			return;
		}

		m_UndoStack.push_back(std::move(command));
		Trim();
		m_RedoStack.clear();
	}

	void UndoRedoManager::Trim()
	{
		while (m_UndoStack.size() > m_MaxHistory)
			m_UndoStack.erase(m_UndoStack.begin());
	}

}

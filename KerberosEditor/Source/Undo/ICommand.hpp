#pragma once

#include <string>

namespace Kerberos {

	class ICommand
	{
	public:
		virtual ~ICommand() = default;

		/// Applies the command's "after" state. Must be idempotent so records that
		/// were already applied by the UI can be replayed safely.
		virtual void Execute() = 0;

		/// Restores the command's "before" state.
		virtual void Undo() = 0;

		/// Stable label used by the Edit menu and debugging.
		virtual const std::string& GetName() const = 0;

		/// Returns true when a newer edit of the same kind may be folded into this command.
		virtual bool CanMerge(const ICommand* /*other*/) const { return false; }

		/// Folds a newer command into this one. Called only after CanMerge returned
		/// true and must leave this command undoable as a single step.
		virtual bool Merge(const ICommand* /*other*/) { return false; }
	};

}

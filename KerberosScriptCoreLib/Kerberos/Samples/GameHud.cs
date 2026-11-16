using System;
using Kerberos.Core;
using Kerberos.Scene;

namespace Kerberos.Samples
{
    /// <summary>
    /// Minimal HUD driver for the editor-authored game UI.
    ///
    /// Create a UI canvas with two Bar elements and a Text prompt, then attach this
    /// script to any entity and assign the element entities to the EntityRef fields
    /// in the inspector.
    /// </summary>
    public class GameHud : Entity
    {
        public Entity? HealthBar;
        public Entity? StaminaBar;
        public Entity? InteractPrompt;

        public float Health = 100.0f;
        public float MaxHealth = 100.0f;
        public float Stamina = 100.0f;
        public float MaxStamina = 100.0f;

        private UIElementComponent? _healthBar;
        private UIElementComponent? _staminaBar;
        private UIElementComponent? _prompt;

        protected override void OnCreate()
        {
            _healthBar = HealthBar?.GetComponent<UIElementComponent>();
            _staminaBar = StaminaBar?.GetComponent<UIElementComponent>();
            _prompt = InteractPrompt?.GetComponent<UIElementComponent>();

            if (_prompt != null)
                _prompt.Visible = false;
        }

        protected override void OnUpdate(float deltaTime)
        {
            // Gameplay input should be ignored while a menu or text field owns the UI.
            if (UI.InputCaptured)
                return;

            if (_healthBar != null && MaxHealth > 0.0f)
                _healthBar.Value = Math.Clamp(Health / MaxHealth, 0.0f, 1.0f);

            if (_staminaBar != null && MaxStamina > 0.0f)
                _staminaBar.Value = Math.Clamp(Stamina / MaxStamina, 0.0f, 1.0f);

            if (_prompt != null)
                _prompt.Visible = Input.IsKeyDown(KeyCode.E);
        }

        /// <summary>Example of applying a renderer setting from a settings menu.</summary>
        public static void ApplyRayTracingSetting(bool enabled)
        {
            if (Graphics.IsRayTracingSupported)
                Graphics.RayQueryShadows = enabled;
        }
    }
}

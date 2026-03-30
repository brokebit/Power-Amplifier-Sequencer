/** @type {import('tailwindcss').Config} */
module.exports = {
  content: [
    '../static/index.html',
    '../static/js/**/*.js',
  ],
  theme: {
    extend: {
      colors: {
        bg:          { primary: 'var(--color-bg-primary)', secondary: 'var(--color-bg-secondary)' },
        text:        { primary: 'var(--color-text-primary)', secondary: 'var(--color-text-secondary)' },
        accent:      'var(--color-accent)',
        success:     'var(--color-success)',
        warning:     'var(--color-warning)',
        danger:      'var(--color-danger)',
        rx:          'var(--color-rx)',
        tx:          'var(--color-tx)',
        sequencing:  'var(--color-sequencing)',
      },
      animation: {
        'pulse-slow': 'pulse-slow 2s ease-in-out infinite',
      },
      keyframes: {
        'pulse-slow': {
          '0%, 100%': { opacity: '1' },
          '50%':      { opacity: '0.6' },
        },
      },
    },
  },
  plugins: [],
};

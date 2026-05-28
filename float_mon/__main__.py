"""包入口：python -m float_mon"""

from .app import FloatingBall


def main():
    app = FloatingBall()
    app.run()


if __name__ == "__main__":
    main()

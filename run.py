"""顶层入口：启动悬浮球监控工具"""

from float_mon.app import FloatingBall


def main():
    app = FloatingBall()
    app.run()


if __name__ == "__main__":
    main()
